/**
 * \file splitclient.c
 *
 * \brief Traffic splitting implementation: code used by the client
 **/

#define MODULE_SPLIT_INTERNAL
#include "feature/split/splitclient.h"

#include "app/config/config.h"
#include "core/or/or.h"
#include "core/or/relay.h"
#include "core/or/circuitbuild.h"
#include "core/or/circuitlist.h"
#include "core/or/circuituse.h"
#include "core/or/connection_edge.h"
#include "core/or/origin_circuit_st.h"
#include "core/or/crypt_path_st.h"
#include "core/or/cpath_build_state_st.h"
#include "core/or/extend_info_st.h"
#include "feature/nodelist/nodelist.h"
#include "feature/split/splitcommon.h"
#include "feature/split/splitdefines.h"
#include "feature/split/spliteval.h"
#include "feature/split/splitstrategy.h"
#include "feature/split/splitutil.h"

#include "lib/crypt_ops/crypto_rand.h"
#include <string.h>

/* Forward declarations */
static int split_data_send_new_cookie(split_data_t* split_data);

/** Check, if the origin_circuit <b>circ</b> should be used for split
 * circuits. Return -1, if check fails; otherwise 0.
 */
static int
split_check_origin_circuit(origin_circuit_t* circ)
{
  if (!circ) {
    return -1;
  }

  if (TO_CIRCUIT(circ)->marked_for_close) {
    log_warn(LD_CIRC, "Circuit %p (ID %u) is marked for close; don't use as "
             "split circuit", circ, TO_CIRCUIT(circ)->n_circ_id);
    return -1;
  }

  if (TO_CIRCUIT(circ)->purpose != CIRCUIT_PURPOSE_C_GENERAL) {
    log_warn(LD_CIRC, "Circuit %p (ID %u) is of purpose '%s'; don't use as "
             "split circuit", circ, TO_CIRCUIT(circ)->n_circ_id,
             circuit_purpose_to_string(TO_CIRCUIT(circ)->purpose));
    return -1;
  }

  if (circ->build_state->onehop_tunnel) {
    log_warn(LD_CIRC, "Circuit %p (ID %u) is a onehop-tunnel; don't use as "
             "split circuit", circ, TO_CIRCUIT(circ)->n_circ_id);
  }

  //TODO-split further checks ?

  return 0;
}

/** Check, if the node <b>middle</b> is actually part of <b>circ</b> and
 * if it should be used as merging node for a split circuit.
 * Return -1, if check fails; otherwise 0.
 */
static int
split_check_circuit_middle(origin_circuit_t* circ, crypt_path_t* middle)
{
  crypt_path_t* cpath;
  int found;

  if (!circ || !middle) {
    return -1;
  }

  /* find middle in circ's cpath */
  cpath = circ->cpath;
  found = 0;
  do {
    tor_assert(cpath);
    if (cpath == middle) {
      found = 1;
      break;
    }
    cpath = cpath->next;
  } while (cpath != circ->cpath);

  if (!found) {
    log_warn(LD_CIRC, "Node %s is not in crypt_path of circ %p (ID %u); "
             "don't use for split circuit", cpath_name(middle), circ,
             TO_CIRCUIT(circ)->n_circ_id);
    return -1;
  }

  if (cpath == circ->cpath || cpath == circ->cpath->prev) {
    /* middle was found, but is in fact entry or exit node */
    log_warn(LD_CIRC, "Node %s is entry or exit node of circ %p (ID %u); "
             "don't use for split circuit", cpath_name(middle), circ,
             TO_CIRCUIT(circ)->n_circ_id);
    return -1;
  }

  if (middle->state != CPATH_STATE_OPEN) {
    log_warn(LD_CIRC, "Circ %p (ID %u) has not been extended to middle %s; "
             "don't use for split circuit", circ, TO_CIRCUIT(circ)->n_circ_id,
             cpath_name(middle));
    return -1;
  }

  //TODO-split further checks ?

  return 0;
}

/** Send a new authentication cookie via <b>client</b> to <b>middle</b>.
 * Do nothing, if we already sent a new cookie, but are still waiting for a
 * response.
 * Payload of cell: |cookie [SPLIT_COOKIE_LEN bytes]|
 * Return 0 on success, -1 on failure.
 */
static int
split_send_new_cookie(origin_circuit_t* circ, crypt_path_t* middle)
{
  split_data_t* split_data;
  char* payload;
  int retval;

  tor_assert(circ);
  tor_assert(middle);
  split_data = middle->split_data;
  tor_assert(split_data);

  if (split_data->cookie_state == SPLIT_COOKIE_STATE_PENDING) {
    /* do nothing */
    return 0;
  }

  split_data->cookie_state = SPLIT_COOKIE_STATE_PENDING;

  SPLIT_MEASURE(circ, split_cookie_start);

  /* generate new cookie*/
  crypto_rand((char*)split_data->cookie, SPLIT_COOKIE_LEN);

  SPLIT_MEASURE(circ, split_cookie_done);

  /* prepare relay cell payload */
  payload = tor_malloc(SPLIT_COOKIE_LEN);
  memcpy(payload, split_data->cookie, SPLIT_COOKIE_LEN);

  log_info(LD_CIRC, "Sending new SET_COOKIE cell on circuit %p (ID %u) to %s "
           "using cookie %s", circ, TO_CIRCUIT(circ)->n_circ_id,
           cpath_name(middle), hex_str(payload, SPLIT_COOKIE_LEN));

  retval = relay_send_command_from_edge(0, TO_CIRCUIT(circ),
                                        RELAY_COMMAND_SPLIT_SET_COOKIE,
                                        payload, SPLIT_COOKIE_LEN, middle);

  tor_free(payload);
  return retval;
}

/** Send a new join request via <b>client</b> to <b>middle</b>.
 * Payload of cell: |cookie [SPLIT_COOKIE_LEN bytes]|
 * Return 0 on success, -1 on failure, and 1 if we first need to set
 * a new cookie.
 */
static int
split_send_join_request(origin_circuit_t* circ, crypt_path_t* middle)
{
  split_data_t* split_data;
  char* payload;
  int retval;

  tor_assert(circ);
  tor_assert(middle);
  split_data = middle->split_data;
  tor_assert(split_data);

  tor_assert(middle->subcirc);
  tor_assert(middle->subcirc->state == SUBCIRC_STATE_PENDING_JOIN);

  /* check if cookie is still valid */
  switch (split_data->cookie_state) {
  case SPLIT_COOKIE_STATE_INVALID:
    log_info(LD_CIRC, "Invalid cookie at split_data %p, set new one",
             split_data);
    subcirc_change_state(middle->subcirc, SUBCIRC_STATE_PENDING_COOKIE);
    if (split_data_send_new_cookie(split_data)) {
      log_info(LD_CIRC, "Unable to send new cookie for split_data %p. "
               "Closing...", split_data);
      /* already marked for close */
    }
    return 1;

  case SPLIT_COOKIE_STATE_PENDING:
    log_info(LD_CIRC, "Already setting new cookie for split_data %p",
             split_data);
    subcirc_change_state(middle->subcirc, SUBCIRC_STATE_PENDING_COOKIE);
    return 1;

  case SPLIT_COOKIE_STATE_VALID:
    /* go on with the function below */
    break;

  default:
    tor_assert_unreached();
  }

  /* prepare relay cell payload */
  payload = tor_malloc(SPLIT_COOKIE_LEN);
  memcpy(payload, split_data->cookie, SPLIT_COOKIE_LEN);

  log_info(LD_CIRC, "Sending new JOIN cell on circuit %p (ID %u) to %s "
             "using cookie %s", circ, TO_CIRCUIT(circ)->n_circ_id,
             cpath_name(middle), hex_str(payload, SPLIT_COOKIE_LEN));

  retval = relay_send_command_from_edge(0, TO_CIRCUIT(circ),
                                        RELAY_COMMAND_SPLIT_JOIN,
                                        payload, SPLIT_COOKIE_LEN, middle);

  tor_free(payload);
  return retval;
}

/** Change the state of <b>subcirc</b> from SUBCIRC_STATE_PENDING_* to
 * SUBCIRC_STATE_ADDED by providing its final sub-circuit <b>id</b> and
 * by moving it from <b>split_data<b>'s pending to its subcircs list.
 */
static void
split_data_subcirc_make_added(split_data_t* split_data, subcircuit_t* subcirc,
                              subcirc_id_t id)
{
  smartlist_t* pending;
  circuit_t* circ;

  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  tor_assert(subcirc);
  tor_assert(subcirc->state == SUBCIRC_STATE_PENDING_COOKIE ||
             subcirc->state == SUBCIRC_STATE_PENDING_JOIN);

  pending = split_data->split_data_client->pending_subcircs;
  tor_assert(smartlist_contains(pending, subcirc));

  circ = subcirc->circ;
  tor_assert(circ);

  if (id == 0)
    tor_assert(circ == split_data->base);

  smartlist_remove(pending, subcirc);

  log_info(LD_CIRC, "Transferring circuit %p (ID %u) from state %s to "
           "SUBCIRC_STATE_ADDED (new index %u)",
            TO_ORIGIN_CIRCUIT(circ), circ->n_circ_id,
           subcirc_state_str(subcirc->state), id);

  subcirc->id = id;
  subcirc->state = SUBCIRC_STATE_ADDED;

  subcirc_list_add(split_data->subcircs, subcirc, id);
  split_data_finalise(split_data);
}

/** Return the cpath of <b>circ</b> which is associated with the given
 * <b>split_data</b>. Never returns NULL, instead uses tor_assert to abort
 * on failure.
 */
static crypt_path_t*
split_data_get_middle_cpath(split_data_t* split_data, origin_circuit_t* circ)
{
  crypt_path_t* cpath;

  tor_assert(split_data);
  tor_assert(circ);

  cpath = circ->cpath;
  do {
    tor_assert(cpath);
    if (cpath->split_data == split_data)
      break;
    cpath = cpath->next;
  } while (cpath != circ->cpath);

  tor_assert(cpath->split_data == split_data);
  return cpath;
}

/** Get the extend_info needed to connect to the merging
 * middle node of <b>split_data</b>
 */
static extend_info_t*
split_data_get_middle_info(split_data_t* split_data)
{
  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  tor_assert(split_data->split_data_client->middle_info);
  return split_data->split_data_client->middle_info;
}

/** Send a new authentication cookie to the middle node that is associated
 * with <b>split_data</b>. For delivery, choose one of the sub-circuits of
 * split_data->subcircs.
 */
static int
split_data_send_new_cookie(split_data_t* split_data)
{
  origin_circuit_t* circ;
  crypt_path_t* cpath;

  tor_assert(split_data);

  if (split_data->cookie_state == SPLIT_COOKIE_STATE_PENDING)
    /* do nothing */
    return 0;

  /* choose a circuit for delivery */
  circ = TO_ORIGIN_CIRCUIT(split_data_get_base(split_data, 1));
  cpath = split_data_get_middle_cpath(split_data, circ);

  return split_send_new_cookie(circ, cpath);
}

/** Launch a new circuit for joining an existing split circuit given through
 * <b>split_data</b>
 */
static origin_circuit_t*
split_data_launch_join_circuit(split_data_t* split_data)
{
  circuit_t* base_circ;
  origin_circuit_t *launched_circ;
  extend_info_t* info;
  cpath_build_state_t* build_state;
  int flags = 0;
  crypt_path_t* middle;
  int err_reason;

  base_circ = split_data_get_base(split_data, 0);
  info = extend_info_dup(split_data_get_middle_info(split_data));

  /* only used to get reference to split_data into circuit-build functions
   * without changing the signature of these functions */
  info->split_data = split_data;

  build_state = TO_ORIGIN_CIRCUIT(base_circ)->build_state;
  tor_assert(build_state);
  tor_assert(!build_state->onehop_tunnel);

  /* we don't neccessarily need a traffic exit node */
  flags |= CIRCLAUNCH_IS_INTERNAL;

  if (build_state->need_uptime)
    flags |= CIRCLAUNCH_NEED_UPTIME;
  if (build_state->need_capacity)
    flags |= CIRCLAUNCH_NEED_CAPACITY;

  log_info(LD_CIRC, "Launching new split sub-circuit for split_data %p",
           split_data);

  launched_circ = circuit_launch_by_extend_info(CIRCUIT_PURPOSE_SPLIT_JOIN,
                                                info, flags);

  extend_info_free(info);

  if (!launched_circ)
      return NULL;

  middle = launched_circ->cpath->prev;
  tor_assert(middle);
  tor_assert(compare_digests(
                middle->extend_info->identity_digest,
                split_data_get_middle_info(split_data)->identity_digest));

  middle->split_data = split_data;
  middle->subcirc = split_data_add_subcirc(split_data,
                                           SUBCIRC_STATE_PENDING_JOIN,
                                           TO_CIRCUIT(launched_circ), 0);

  /* now we may call circuit_handle_first_hop for the new SPLIT_JOIN
   * circuit*/
  if ((err_reason = circuit_handle_first_hop(launched_circ)) < 0) {
    circuit_mark_for_close(TO_CIRCUIT(launched_circ), -err_reason);
    return NULL;
  }

  return launched_circ;
}

/** Launch and add a new sub-circuit to <b>split_data</b>.
 */
static void
split_data_launch_subcirc(split_data_t* split_data, int num)
{
  origin_circuit_t* launched_circ;

  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  tor_assert(num >= 0);

  if (num == 0)
    return;

  if (split_data->marked_for_close) {
    log_info(LD_CIRC, "split_data %p was marked for close, cannot launch "
             "new sub-circuits", split_data);
    return;
  }

  if (split_data_get_num_subcircs(split_data) + num > MAX_SUBCIRCS) {
    log_info(LD_CIRC, "split_data %p already reached its maximum number of "
             "%d sub-circuits", split_data, MAX_SUBCIRCS);
    return;
  }

  switch (split_data->cookie_state) {
  case SPLIT_COOKIE_STATE_INVALID:
    log_info(LD_CIRC, "Invalid cookie at split_data %p, set new one",
             split_data);
    if (split_data_send_new_cookie(split_data)) {
      log_info(LD_CIRC, "Unable to send new cookie for split_data %p. "
               "Closing...", split_data);
      /* already marked for close */
      return;
    }
    break;

  case SPLIT_COOKIE_STATE_PENDING:
  case SPLIT_COOKIE_STATE_VALID:
    log_info(LD_CIRC, "Lauching %d new sub-circuits of split_data %p",
             num, split_data);
    /* go on with the function below */
    break;

  default:
    tor_assert_unreached();
  }

  for (int i = 0; i < num; i++) {
    launched_circ = split_data_launch_join_circuit(split_data);
    if (!launched_circ) {
      log_info(LD_CIRC, "Launching new split sub-circuit failed. Retry later?");
      //TODO-split retry
      return;
    }
  }
}

/** Called when received a COOKIE_SET successful cell to try and
 * launch/join circuits that were waiting for that cookie
 */
static void
split_data_handle_pending_cookie(split_data_t* split_data)
{
  unsigned int num_to_launch;
  smartlist_t* pending_subcircs;
  origin_circuit_t* circ;
  crypt_path_t* cpath;

  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  tor_assert(split_data->cookie_state == SPLIT_COOKIE_STATE_VALID);

  /* launch requested number of sub-circuits */
  num_to_launch = split_data->split_data_client->launch_on_cookie;
  split_data->split_data_client->launch_on_cookie = 0;

  split_data_launch_subcirc(split_data, num_to_launch);

  /* send new join request for existing sub-circuits in pending_subcircs */
  pending_subcircs = split_data->split_data_client->pending_subcircs;
  SMARTLIST_FOREACH_BEGIN(pending_subcircs, subcircuit_t*, subcirc) {
    tor_assert(subcirc);
    //DEBUG-split
    tor_assert(split_data_check_subcirc(split_data, subcirc->circ) == 1);

    if (subcirc->state != SUBCIRC_STATE_PENDING_COOKIE)
      continue;

    subcirc_change_state(subcirc, SUBCIRC_STATE_PENDING_JOIN);
    circ = TO_ORIGIN_CIRCUIT(subcirc->circ);
    cpath = split_data_get_middle_cpath(split_data, circ);
    if (split_send_join_request(circ, cpath) < 0) {
      log_info(LD_CIRC, "Unable to send join request to %s (split_data %p) "
               "on circuit %p (ID %u). Closing...", cpath_name(cpath),
               split_data, circ, TO_CIRCUIT(circ)->n_circ_id);
      /* already marked for close */
    }

  } SMARTLIST_FOREACH_END(subcirc);
}

static void
split_data_append_cpath(split_data_t* split_data, origin_circuit_t* circ)
{
  crypt_path_t *source, *cpath, *new;

  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  tor_assert(circ);
  tor_assert(TO_CIRCUIT(circ)->purpose == CIRCUIT_PURPOSE_SPLIT_JOIN);

  source = split_data->split_data_client->remaining_cpath;
  cpath = source;

  do {
    tor_assert(cpath);

    new = tor_malloc_zero(sizeof(crypt_path_t));
    new->magic = CRYPT_PATH_MAGIC;
    new->state = CPATH_STATE_OPEN;
    new->extend_info = extend_info_dup(cpath->extend_info);
    memcpy(&new->crypto, &cpath->crypto, sizeof(relay_crypto_t));
    tor_assert(new->crypto.ref_count);
    *new->crypto.ref_count += 1;

    onion_append_to_cpath(&circ->cpath, new);

    cpath = cpath->next;
  } while (cpath != source);

  log_info(LD_CIRC, "Appended cpath of circ %p (ID %u): %s", circ,
           TO_CIRCUIT(circ)->n_circ_id, circuit_list_path(circ, 1));
}

/** Turn <b>circ</b> into a split circuit merging at <b>middle</b>
 * by creating a new split_data structure, associating (circ,middle)
 * with this structure and sending an initial cookie.
 */
static int
split_create_split_data(origin_circuit_t* circ, crypt_path_t* middle)
{
  split_data_t* split_data;

  tor_assert(circ);
  tor_assert(middle);
  tor_assert(!middle->split_data);
  tor_assert(!middle->subcirc);

  //DEBUG-tor
  tor_assert(!split_check_origin_circuit(circ));
  tor_assert(!split_check_circuit_middle(circ, middle));

  split_data = split_data_new();
  split_data_init_client(split_data, circ, middle);

  middle->split_data = split_data;
  middle->subcirc = split_data_add_subcirc(split_data,
                                           SUBCIRC_STATE_PENDING_COOKIE,
                                           TO_CIRCUIT(circ), 0);

  SPLIT_MEASURE(circ, split_data_created);

  return split_send_new_cookie(circ, middle);
}

/** Launch <b>num</b> new sub-circuits for the circuit <b>circ</b> which
 * merges at <b>middle</b>. If circ isn't already a split circuit, turn
 * it into one.
 * Return -1 on failure; otherwise 0.
 */
int
split_launch_subcircuit(origin_circuit_t* circ, crypt_path_t* middle, int num)
{
  if (num <= 0)
    return 0;

  if (num >= MAX_SUBCIRCS) {
    log_warn(LD_CIRC, "Cannot launch more than MAX_SUBCIRCS sub-circuits");
    return -1;
  }

  if (split_check_origin_circuit(circ)) {
    log_warn(LD_CIRC, "Circuit %p (ID %u) not suited as split circuit. "
             "Aborting...", circ, TO_CIRCUIT(circ)->n_circ_id);
    return -1;
  }

  if (split_check_circuit_middle(circ, middle)) {
    log_warn(LD_CIRC, "Cannot add new sub-circuit to circ %p (ID %u) at "
             "middle %p", circ, TO_CIRCUIT(circ)->n_circ_id,
             cpath_name(middle));
    return -1;
  }

  if (!middle->split_data) {
    /* create split_data and return -1 on failure */
    if (split_create_split_data(circ, middle))
      return -1;
  } else {
    tor_assert(split_data_check_subcirc(middle->split_data,
                                        TO_CIRCUIT(circ))
        == 0);
  }

  split_data_launch_subcirc(middle->split_data, num);
  return 0;
}

/** Process a COOKIE_SET cell (with <b>payload</b> and <b>length</b>) that was
 * received on circuit <b>circ</b> from <b>middle</b>.
 * Return -1 on failure; otherwise 0.
 */
int
split_process_cookie_set(origin_circuit_t* circ, crypt_path_t* middle,
                         size_t length, const uint8_t* payload)
{
  uint8_t success;
  split_data_t* split_data;
  subcircuit_t* subcirc;
  subcirc_id_t received_id;
  size_t id_length = sizeof(subcirc_id_t);

  tor_assert(circ);
  tor_assert(middle);
  tor_assert(payload);

  if (length != 1 && length != 1 + id_length) {
    log_warn(LD_CIRC, "Received COOKIE_SET cell on circuit %p (ID %u) with "
             "wrong length %u. Closing...", circ, TO_CIRCUIT(circ)->n_circ_id,
             (unsigned int)length);
    goto err_close;
  }

  success = payload[0];

  log_info(LD_CIRC, "Received COOKIE_SET %s cell on circuit %p (ID %u) with "
           "payload %s", success ? "(success)" : "(failure)",
            circ, TO_CIRCUIT(circ)->n_circ_id,
            hex_str((const char*)payload, length));

  split_data = middle->split_data;
  subcirc = middle->subcirc;

  if (!split_data) {
    tor_assert_nonfatal(!subcirc);
    log_info(LD_CIRC, "Cannot process COOKIE_SET as there is no split_data."
             "Closing...");
    goto err_close;
  }

  tor_assert(split_data);
  tor_assert(subcirc);

  if (split_data->cookie_state != SPLIT_COOKIE_STATE_PENDING) {
    log_info(LD_CIRC, "Cookie state wasn't \"pending\". Closing...");
    goto err_close;
  }

  if (success) {
    tor_assert(length == 1 + id_length);
    received_id = subcirc_id_ntoh(read_subcirc_id(payload + 1));

    if (subcirc->state == SUBCIRC_STATE_PENDING_COOKIE) {
      /* this can only happen during setting the initial cookie, as in all
         other cases the circuit sending and receiving the cookie set-up
         cells must be already added.
         Therefore, make this subcirc added. */
      tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 1);
      split_data_subcirc_make_added(split_data, subcirc, received_id);

    } else if (subcirc->state == SUBCIRC_STATE_ADDED) {
      /* all other cases; circ is already added to split_data */
      tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 0);

      if (middle->subcirc->id != received_id) {
        log_warn(LD_CIRC, "COOKIE_SET cell contains sub-circuit ID %u, but we "
                 "already are a sub-circuit of split_data %p with ID %u. "
                 "Closing...",  received_id, split_data, middle->subcirc->id);
        goto err_close;
      }
    } else {
      /* subcirc should not be in other state when we receive a
         COOKIE_SET cell */
      tor_assert_unreached();
    }

    /* update cookie state */
    split_data->cookie_state = SPLIT_COOKIE_STATE_VALID;

    split_data_handle_pending_cookie(split_data);

  } else { /* success */
    tor_assert(length == 1);

    if (subcirc->state == SUBCIRC_STATE_PENDING_COOKIE) {
      /* this can only happen during setting the initial cookie, as in all
         other cases the circuit sending and receiving the cookie set-up
         cells must be already added.
         Therefore check some assumptions and remove split_data */
      tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 1);
      tor_assert(split_data_get_num_subcircs(split_data) == 1);

      split_data_remove_subcirc(&middle->split_data, &middle->subcirc, 0);
    } else if (subcirc->state == SUBCIRC_STATE_ADDED) {
      /* all other cases; circ is already added to split_data */
      tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 0);
      split_data->cookie_state = SPLIT_COOKIE_STATE_INVALID;
    } else {
      /* subcirc should not be in other state when we receive a
         COOKIE_SET cell */
      tor_assert_unreached();
    }
  }

  return 0;

 err_close:
  circuit_mark_for_close(TO_CIRCUIT(circ), END_CIRC_REASON_TORPROTOCOL);
  return -1;
}

/** Add all member node_t's of <b>circ</b> to <b>excluded</b>. Do nothing,
 * if circ is NULL.
 */
static void
split_circuit_add_excluded(smartlist_t* excluded, origin_circuit_t* circ)
{
  crypt_path_t* cpath;
  const node_t* node;

  tor_assert(excluded);

  if (!circ)
    return;

  cpath = circ->cpath;

  do {
    tor_assert(cpath);

    node = node_get_by_id(cpath->extend_info->identity_digest);
    nodelist_add_node_and_family(excluded, node);

    cpath = cpath->next;
  } while (cpath != circ->cpath);
}

/** Return a new smartlist_t of const node_t* containing all nodes
 * that are currently used by circuits associated with <b>split_data<b>.
 * Returns NULL, if split_data is NULL.
 */
smartlist_t*
split_data_get_excluded_nodes(split_data_t* split_data)
{
  smartlist_t *excluded, *pending;
  circuit_t* base_circ;
  origin_circuit_t* circ;
  crypt_path_t* cpath;
  split_data_t* aux;
  subcircuit_t* subcirc;

  if (!split_data)
    return NULL;

  log_info(LD_CIRC, "Begin creating exclude list for split_data %p",
           split_data);

  excluded = smartlist_new();

  base_circ = split_data_get_base(split_data, 0);
  circ = TO_ORIGIN_CIRCUIT(base_circ);

#ifdef SPLIT_GENERATE_EXCLUDE
  cpath = circ->cpath;
  do {
    tor_assert(cpath);

    aux = cpath->split_data;
    if (aux) {
      for (subcirc_id_t id = 0; (int)id <= aux->subcircs->max_index; id++) {
        subcirc = split_data_get_subcirc(aux, id);
        if (subcirc) {
          tor_assert(subcirc->circ); //DEBUG-split
          split_circuit_add_excluded(excluded,
              TO_ORIGIN_CIRCUIT(subcirc->circ));
        }
      }

      tor_assert(aux->split_data_client);
      pending = aux->split_data_client->pending_subcircs;
      tor_assert(pending);
      SMARTLIST_FOREACH_BEGIN(pending, subcircuit_t*, sub) {
        tor_assert(sub);
        tor_assert(sub->circ);
        split_circuit_add_excluded(excluded,
            TO_ORIGIN_CIRCUIT(sub->circ));

      } SMARTLIST_FOREACH_END(sub);
    } /* aux */

    cpath = cpath->next;
  } while (cpath != circ->cpath);
#else /* !defined(SPLIT_GENERATE_EXCLUDE) */
  /* still exclude the all nodes behind the middle node */
  const node_t* node;
  (void)pending;
  (void)aux;
  (void)subcirc;
  (void)&split_circuit_add_excluded;

  cpath = split_data_get_middle_cpath(split_data, circ)->next;
  do {
      tor_assert(cpath);

      node = node_get_by_id(cpath->extend_info->identity_digest);
      nodelist_add_node_and_family(excluded, node);

      cpath = cpath->next;
    } while (cpath != circ->cpath);
#endif /* SPLIT_GENERATE_EXCLUDE */

  log_info(LD_CIRC, "Finished creating exclude list for split_data %p",
           split_data);

  return excluded;
}

/** Called when a circuit of type SPLIT_JOIN was successfully opened */
void
split_join_has_opened(origin_circuit_t* circ)
{
  crypt_path_t* middle;

  tor_assert(circ);
  tor_assert(circ->cpath);
  middle = circ->cpath->prev;

  tor_assert(middle->split_data);
  tor_assert(middle->subcirc);
  tor_assert(middle->subcirc->state == SUBCIRC_STATE_PENDING_JOIN);

  if (split_send_join_request(circ, middle) < 0) {
    log_info(LD_CIRC, "Unable to send join request to %s (split_data %p) "
             "on circuit %p (ID %u). Closing...", cpath_name(middle),
             middle->split_data, circ, TO_CIRCUIT(circ)->n_circ_id);
    /* already marked for close */
  }
}

/** Process a JOINED cell (with <b>payload</b> and <b>length</b>) that was
 * received on circuit <b>circ</b> from <b>middle</b>.
 * Return -1 on failure; otherwise 0.
 */
int
split_process_joined(origin_circuit_t* circ, crypt_path_t* middle,
                   size_t length, const uint8_t* payload)
{
  uint8_t success;
  split_data_t* split_data;
  subcircuit_t* subcirc;
  subcirc_id_t received_id;
  size_t id_length = sizeof(subcirc_id_t);

  tor_assert(circ);
  tor_assert(middle);
  tor_assert(payload);

  if (length != 1 && length != 1 + id_length) {
    log_warn(LD_CIRC, "Received JOINED cell on circuit %p (ID %u) with "
             "wrong length %u. Closing...", circ, TO_CIRCUIT(circ)->n_circ_id,
             (unsigned int)length);
    goto err_close;
  }

  success = payload[0];

  log_info(LD_CIRC, "Received JOINED %s cell on circuit %p (ID %u) with "
           "payload %s", success ? "(success)" : "(failure)",
            circ, TO_CIRCUIT(circ)->n_circ_id,
            hex_str((const char*)payload, length));

  split_data = middle->split_data;
  subcirc = middle->subcirc;

  if (!split_data) {
    tor_assert_nonfatal(!subcirc);
    log_info(LD_CIRC, "Cannot process JOINED as there is no split_data."
             "Closing...");
    goto err_close;
  }

  tor_assert(split_data);
  tor_assert(subcirc);

  if (subcirc->state != SUBCIRC_STATE_PENDING_JOIN) {
    log_info(LD_CIRC, "Cookie state wasn't \"pending\". Closing...");
     goto err_close;
  }

  //DEBUG-split
  tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 1);

  if (success) {
    tor_assert(length == 1 + id_length);
    received_id = subcirc_id_ntoh(read_subcirc_id(payload + 1));

    split_data_append_cpath(split_data, circ);
    split_data_subcirc_make_added(split_data, subcirc, received_id);

    /* consider attaching streams to the base circuit now */
    circuit_t* base_circ = split_data_get_base(split_data, 1);
    if (split_may_attach_stream(TO_ORIGIN_CIRCUIT(base_circ), 1)) {
      connection_ap_attach_pending(1);
    }

  } else { /* success */
    tor_assert(length == 1);

    /* we received this message, because the or/middle-side cookie was
     * invalid; so send a new one... */
    split_data_send_new_cookie(split_data);
    subcirc_change_state(subcirc, SUBCIRC_STATE_PENDING_COOKIE);
  }

  return 0;

 err_close:
  circuit_mark_for_close(TO_CIRCUIT(circ), END_CIRC_REASON_TORPROTOCOL);
  return -1;
}

/** Return TRUE if streams may be attached to the given <b>circ</b>.
 * Otherwise, return FALSE. When <b>must_be_open</b> is TRUE, only
 * those split circuits should be used that have a reasonably advanced
 * build status.
 */
int
split_may_attach_stream(const origin_circuit_t* circ, int must_be_open)
{
  int may_attach;
  crypt_path_t* cpath;
  tor_assert(circ);

  if (TO_CIRCUIT(circ)->purpose == CIRCUIT_PURPOSE_SPLIT_JOIN)
    return 0;

  if (!must_be_open)
    return 1;

  may_attach = 1;
  cpath = circ->cpath;
  do {
    tor_assert(cpath);
    if (cpath->split_data) {
      tor_assert(cpath->subcirc); //DEBUG-split
      tor_assert(cpath->split_data->split_data_client);
      split_data_finalise(cpath->split_data);
      if (!cpath->split_data->split_data_client->is_final) {
        may_attach = 0;
        break;
      }
    }
    cpath = cpath->next;
  } while (cpath != circ->cpath);

  if (!may_attach) {
    log_info(LD_CIRC, "Not all split_data structs for split circ "
             "%p are marked as final. Cannot attach streams...", circ);
  }

  return may_attach;
}


/* Generate a new split instruction for <b>split_data</b> in <b>direction</b>
 * and notify the corresponding middle node via split_data's base circuit.
 * Return -1 on failure; 0 on success.
 * (Assumes that the base circuit has already been correctly added to
 * split_data.)
 */
int
split_data_generate_instruction(split_data_t* split_data,
                                cell_direction_t direction)
{
  circuit_t* base;
  split_instruction_t* new_instruction;
  split_instruction_t** existing_instructions;
  uint8_t relay_command = 0;
  uint8_t* payload = NULL;
  ssize_t payload_len;
  crypt_path_t* cpath;
  int retval;

  tor_assert(split_data);
  tor_assert(split_data->split_data_client);
  base = split_data_get_base(split_data, 1);
  tor_assert(CIRCUIT_IS_ORIGIN(base));
  int use_prev_data = 0;
  double prev_data[MAX_SUBCIRCS];
  switch (direction) {
    case CELL_DIRECTION_IN:
      existing_instructions = &split_data->instruction_in;
      relay_command = RELAY_COMMAND_SPLIT_INSTRUCTION;
      use_prev_data = split_data->split_data_client->use_previous_data_in;
      for (int h = 0; h < MAX_SUBCIRCS ; h++)
           prev_data[h] = split_data->split_data_client->previous_data_in[h];
      break;
    case CELL_DIRECTION_OUT:
      existing_instructions = &split_data->instruction_out;
      relay_command = RELAY_COMMAND_SPLIT_INFO;
      use_prev_data = split_data->split_data_client->use_previous_data_out;
      for (int h = 0; h < MAX_SUBCIRCS ; h++)
          prev_data[h] = split_data->split_data_client->previous_data_out[h];
      break;
    default:
      tor_assert_unreached();
  }

  /* generate new split instruction */

  if (BUG(split_instruction_list_length(*existing_instructions) >=
      MAX_NUM_SPLIT_INSTRUCTIONS)) {
    /* do not overload the middle's memory by sending too many
     * split instructions */
    log_warn(LD_CIRC, "We have already created too many split "
             "instructions.");
    return -1;
  }

  new_instruction =
      split_get_new_instruction(split_data->split_data_client->strategy,
                                split_data->subcircs, direction, use_prev_data, prev_data);
  /* Keep track of the data previously used, when use_prev_data == 1, we are still on the same page load and we must use the same 
   * dirichlet vector. This is only used for WR and BWR.
   */
  switch (direction) {
    case CELL_DIRECTION_IN:
      memcpy(split_data->split_data_client->previous_data_in,prev_data,
             sizeof(split_data->split_data_client->previous_data_in));
      break;
    case CELL_DIRECTION_OUT:
      memcpy(split_data->split_data_client->previous_data_out,prev_data,
             sizeof(split_data->split_data_client->previous_data_out));
      break;
    default:
      tor_assert_unreached();
  }
  /* notify middle node */
  payload_len = split_instruction_to_payload(new_instruction, &payload);
  if (payload_len < 0)
    return -1;
  tor_assert(payload_len > 0);

  cpath = split_data_get_middle_cpath(split_data, TO_ORIGIN_CIRCUIT(base));

  log_info(LD_CIRC, "Sending new %s cell on circuit %p (ID %u) to %s ",
           relay_command == RELAY_COMMAND_SPLIT_INSTRUCTION ? "INSTRUCTION" :
           "INFO", TO_ORIGIN_CIRCUIT(base), base->n_circ_id,
           cpath_name(cpath));

  retval = relay_send_command_from_edge(0, base, relay_command,
                                         (char*)payload, payload_len, cpath);

  tor_free(payload);

  /* only append new instruction here to keep being in a defined state when
   * an error occurs above. */
  split_instruction_append(existing_instructions, new_instruction);
  return retval;
}

/* Mark the given <b>split_data</b> as final (if it fulfils the required
 * conditions) to make streams attachable to it.
 */
void
split_data_finalise(split_data_t* split_data)
{
  tor_assert(split_data);
  tor_assert(split_data->split_data_client);

  if (split_data->split_data_client->is_final) {
    /* already finalised */
    return;
  }

  if (split_data_get_num_subcircs_added(split_data) <
          split_get_subcircs_per_circ()
      && split_data_get_num_subcircs_pending(split_data) +
          split_data->split_data_client->launch_on_cookie > 0)
    return;

  log_info(LD_CIRC, "Make split_data %p final", split_data);
  split_data->split_data_client->use_previous_data_in = 0;
  split_data->split_data_client->use_previous_data_out = 0; //this is the beginning of the page load and therefore data distribution is enterely new

  for (int i = 0; i < NUM_SPLIT_INSTRUCTIONS; i++){
      split_data_generate_instruction(split_data, CELL_DIRECTION_IN);
      split_data->split_data_client->use_previous_data_in = 1;
  }
  for (int i = 0; i < NUM_SPLIT_INSTRUCTIONS; i++){
      split_data_generate_instruction(split_data, CELL_DIRECTION_OUT);
      split_data->split_data_client->use_previous_data_out = 1;
  }
 

  split_data->split_data_client->is_final = 1;
}

/** Write the name of the next network interface (e.g., "eth0") to
 * use for sub-circuits added to <b>base</b> as null-terminated string
 * into (of maximum size <b>len</b>) into <b>if_name</b>.
 * Writes an empty string, if an arbitrary interface may be used.
 * (The caller must ensure that len is large enough; we recommend using
 * at least IFNAMSIZ bytes.)
 */
void
split_next_if_name(origin_circuit_t* base, char* if_name, size_t len)
{
  tor_assert(base);
  tor_assert(if_name);
  tor_assert(len > 0);

  strlcpy(if_name, SPLIT_DEFAULT_INTERFACE, len);
}

/** Based on the current configuration, return the desired number of
 * sub-circuits per circuit */
unsigned int
split_get_subcircs_per_circ(void)
{
  const or_options_t* options = get_options();

  if (options->SplitSubcircuits >= 1 &&
      options->SplitSubcircuits <= MAX_SUBCIRCS)
    return (unsigned int)options->SplitSubcircuits;

  return SPLIT_DEFAULT_SUBCIRCS;
}
