/**
 * \file splitcommon.c
 *
 * \brief Traffic splitting implementation: shared code between clients
 *  and ORs
 **/

#define MODULE_SPLIT_INTERNAL
#include "feature/split/splitcommon.h"

#include "core/or/or.h"
#include "core/or/cell_st.h"
#include "core/or/circuitbuild.h"
#include "core/or/circuitlist.h"
#include "core/or/circuituse.h"
#include "core/or/relay.h"
#include "core/or/circuit_st.h"
#include "core/or/or_circuit_st.h"
#include "core/or/origin_circuit_st.h"
#include "core/or/extend_info_st.h"
#include "feature/control/control.h"
#include "feature/split/cell_buffer.h"
#include "feature/split/splitclient.h"
#include "feature/split/splitdefines.h"
#include "feature/split/spliteval.h"
#include "feature/split/splitor.h"
#include "feature/split/splitstrategy.h"
#include "feature/split/splitutil.h"
#include "feature/split/subcirc_list.h"
#include "feature/split/split_data_st.h"
#include "feature/split/subcircuit_st.h"
#include "core/or/channeltls.h" //wdlc

/** Allocate a new split_data_t structure and return a pointer (never returns
 * NULL, if 'split' module is activated)
 *
 * WARNING: always returns NULL, if 'split' module is deactivated
 */
split_data_t*
split_data_new(void)
{
  split_data_t* split_data;
  split_data = tor_malloc_zero(sizeof(split_data_t));

  log_info(LD_CIRC, "New split_data %p was created", split_data);
  return split_data;
}

/** Initialize a given <b>split_data</b> structure with default values.
 */
static void
split_data_init(split_data_t* split_data, circuit_t* base)
{
  tor_assert(split_data);
  tor_assert(base);

  split_data->base = base;
  split_data->cookie_state = SPLIT_COOKIE_STATE_INVALID;
  split_data->subcircs = subcirc_list_new();

  if (CIRCUIT_IS_ORIGIN(base)) {
    origin_circuit_t* origin_base = TO_ORIGIN_CIRCUIT(base);
    if (!origin_base->split_data_circuit)
      origin_base->split_data_circuit = split_data_circuit_new();
    origin_base->split_data_circuit->num_split_data += 1;
  }
}

/** Initialize a given <b>split_data</b> structure for the client side.
 * (<b>base</b>, <b>middle</b>) gives the combination of circuit/middle
 * for which split_data was created.
 */
void
split_data_init_client(split_data_t* split_data, origin_circuit_t* base,
                       crypt_path_t* middle)
{
  split_data_init(split_data, TO_CIRCUIT(base));

  split_data->split_data_client = split_data_client_new();
  split_data_client_init(split_data->split_data_client, base, middle);
}

/** Initialize a given <b>split_data</b> structure for the or/middle side.
 */
void
split_data_init_or(split_data_t* split_data, or_circuit_t* base)
{
  split_data_init(split_data, TO_CIRCUIT(base));

  split_data->split_data_or = split_data_or_new();
  split_data_or_init(split_data->split_data_or, split_data, base);
}

/** Deallocate the memory associated with <b>split_data</b>
 */
void
split_data_free_(split_data_t* split_data)
{
  if (!split_data)
    return;

  /* deinitialisation of struct members */
  split_data_client_free(split_data->split_data_client);
  split_data_or_free(split_data->split_data_or);
  subcirc_list_free(split_data->subcircs);
  split_instruction_free_list(&split_data->instruction_out);
  split_instruction_free_list(&split_data->instruction_in);

  log_info(LD_CIRC, "Split_data %p was deallocated", split_data);
  tor_free(split_data);
}

/** Return the subcircuit_t associated with <b>split_data</b> and with
 * sub-circuit ID <b>id</b>. Return NULL, if no such sub-circuit exists.
 */
subcircuit_t*
split_data_get_subcirc(split_data_t* split_data, subcirc_id_t id)
{
  tor_assert(split_data);

  if (!split_data_get_num_subcircs_added(split_data))
    return NULL;

  tor_assert(split_data->subcircs->max_index >= 0);
  if (id > split_data->subcircs->max_index)
    return NULL;

  return subcirc_list_get(split_data->subcircs, id);
}

/** Return a pointer to the base circuit of the given <b>split_data</b>.
 * The base circuit is the circuit that was originally used to establish
 * the split circuit. If <b>must_be_added</b> is TRUE, the base circuit
 * must have been added to the split circuit.
 */
circuit_t*
split_data_get_base(split_data_t* split_data, int must_be_added)
{
  circuit_t* base;
  subcircuit_t* subcirc;

  tor_assert(split_data);
  base = split_data->base;

  tor_assert(base);
  tor_assert(base->purpose != CIRCUIT_PURPOSE_SPLIT_JOIN);

  /* the base circuit is the first one to be added, so it should always
   * have the index 0 */
  subcirc = split_data_get_subcirc(split_data, 0);

  if (must_be_added) {
    tor_assert(subcirc);
    tor_assert(subcirc->state == SUBCIRC_STATE_ADDED);
  }

  if (subcirc) {
    tor_assert(base == subcirc->circ);
  }

  return base;
}

/** Return the number of sub-circuits that are currently somehow associated
 * with <b>split_data</b> (includes added and pending subcircs)
 */
unsigned int
split_data_get_num_subcircs(split_data_t* split_data)
{
  unsigned int pending = split_data_get_num_subcircs_pending(split_data);
  unsigned int added = split_data_get_num_subcircs_added(split_data);
  return pending + added;
}

/** Return the number of pending sub-circuit that are currently associated
 * with <b>split_data</b>
 */
unsigned int
split_data_get_num_subcircs_pending(split_data_t* split_data)
{
  tor_assert(split_data);

  if (split_data->split_data_client) {
    tor_assert(split_data->split_data_client->pending_subcircs);
    int len = smartlist_len(split_data->split_data_client->pending_subcircs);
    return (unsigned int)len;
  }

  return 0;
}

/** Return the number of sub-circuits that have been successfully added
 * to <b>split_data</b>
 */
unsigned int
split_data_get_num_subcircs_added(split_data_t* split_data)
{
  tor_assert(split_data);
  return subcirc_list_get_num(split_data->subcircs);
}

/** Create a new subcirc and initialise it with <b>state</b>,
 * <b>circ</b>, and <b>id</b>. Depending on state, add this
 * new subcirc either to split_data's subcircs list (containing
 * the correctly associated subcircs) or to split_data->split_data_client's
 * pending_subcircs (containing all subcircs that are currently being
 * added to split_data).
 * Return a pointer to the newly created subcircuit_t to easily add it to
 * the corresponding cpath/or_circuit structure.
 */
subcircuit_t*
split_data_add_subcirc(split_data_t* split_data, subcirc_state_t state,
                       circuit_t* circ, subcirc_id_t id)
{
  subcircuit_t* subcirc;

  tor_assert(split_data);
  tor_assert(circ);

  if (split_data->marked_for_close) {
    log_warn(LD_CIRC, "split_data %p already marked for close, cannot add "
             "further sub-circuits", split_data);
    return NULL;
  }

  tor_assert(split_data_check_subcirc(split_data, circ) == 2);

  subcirc = subcircuit_new();
  subcirc->circ = circ;
  subcirc->id = id;
  subcirc->state = state;

  switch (state) {
    case SUBCIRC_STATE_PENDING_COOKIE:
    case SUBCIRC_STATE_PENDING_JOIN:
      tor_assert(split_data->split_data_client);
      tor_assert(split_data->split_data_client->pending_subcircs);
      smartlist_add(split_data->split_data_client->pending_subcircs, subcirc);
      log_info(LD_CIRC, "Added circ %p (ID %u) to the pending sub-circuits "
               "of split_data %p (state %s)", TO_ORIGIN_CIRCUIT(circ),
               circ ? circ->n_circ_id : 0, split_data,
               subcirc_state_str(state));
      break;

    case SUBCIRC_STATE_ADDED:
      tor_assert(subcirc->circ);
      if (id == 0)
        tor_assert(circ == split_data->base);
      subcirc_list_add(split_data->subcircs, subcirc, subcirc->id);
      split_data_reset_next_subcirc(split_data);
      log_info(LD_CIRC, "Added circ %p (ID %u) with index %u to "
               "split_data %p",
               CIRCUIT_IS_ORCIRC(circ) ? (void*)TO_OR_CIRCUIT(circ) :
                   (void*)TO_ORIGIN_CIRCUIT(circ),
               CIRCUIT_IS_ORCIRC(circ) ? TO_OR_CIRCUIT(circ)->p_circ_id :
                   circ->n_circ_id,
               id, split_data);
      break;

    case SUBCIRC_STATE_UNSPEC:
    default:
      log_warn(LD_CIRC, "Cannot add subcirc with unspecified state %s (%d)",
               subcirc_state_str(state), state);
      subcircuit_free(subcirc);
  }

  return subcirc;
}

/** Check, if <b>circ</b> is already associated with <b>split_data</b>.
 * Return -1, if there is a incomplete association
 * Return 0, if circ has been correctly added to split_data
 * Return 1, if circ is pending to be added to split_data
 * Return 2, if circ is not associated with split_data
 */
int
split_data_check_subcirc(split_data_t* split_data, circuit_t* circ)
{
  //TODO-split maybe reduce complexity of checks later?
  subcircuit_t* subcirc = NULL;
  origin_circuit_t* origin_circ = NULL;
  or_circuit_t* or_circ = NULL;

  tor_assert(split_data);
  tor_assert(circ);

  /* find subcirc belonging to circ */
  if (CIRCUIT_IS_ORCIRC(circ)) {
    /* we're at the merging middle */
    or_circ = TO_OR_CIRCUIT(circ);
    if (or_circ->split_data == split_data) {
      subcirc = or_circ->subcirc;
    }
  } else {
    /* we're at the client */
    crypt_path_t* cpath = NULL;
    crypt_path_t* tmp;

    origin_circ = TO_ORIGIN_CIRCUIT(circ);

    /* find cpath corresponding to split_data */
    tmp = origin_circ->cpath;
    do {
      tor_assert(tmp);
      if (tmp->split_data == split_data) {
        cpath = tmp;
        break;
     }
     tmp = tmp->next;
    } while (tmp != origin_circ->cpath);

    if (cpath)
      subcirc = cpath->subcirc;
  }

  if (!subcirc) {
    /* be sure that circ is not associated with split_data via an other
       subcirc; might take quite long... */

    /* check split_data->subcircs */
    for (subcirc_id_t id = 0;
         (int) id <= split_data->subcircs->max_index; id++) {
      subcircuit_t* aux = split_data_get_subcirc(split_data, id);
      if (aux && aux->circ == circ) {
        log_warn(LD_CIRC, "Found circ %p (ID %u) in split_data %p, but "
                 "circ's subcirc member was not correctly set",
                 or_circ ? (void*)or_circ : (void*)origin_circ,
                 or_circ ? or_circ->p_circ_id : circ->n_circ_id,
                 split_data);
        return -1;
      }
    }

    if (split_data->split_data_client) {
      /* check split_data->split_data_client->pending_subcircs */
      tor_assert(origin_circ);
      tor_assert(split_data->split_data_client->pending_subcircs);
      smartlist_t* list = split_data->split_data_client->pending_subcircs;
      SMARTLIST_FOREACH_BEGIN(list, subcircuit_t*, aux) {
        tor_assert(aux);
        if (aux->circ == circ) {
          log_warn(LD_CIRC, "Found circ %p (ID %u) in the pending_subcircs of "
                   "split_data %p, but circ's subcirc member was not "
                   "correctly set", origin_circ, circ->n_circ_id, split_data);
          return -1;
        }
      } SMARTLIST_FOREACH_END(aux);
    }

    /* circ not associated with split_data */
    return 2;
  }

  /* subcirc was found, but is it actually registered with split_data and
     circ? */
  if (subcirc->circ != circ) {
    log_warn(LD_CIRC, "subcirc->circ %u associated with circ %p (ID %u), "
             "but reference is missing", subcirc->id,
             or_circ ? (void*)or_circ : (void*)origin_circ,
             or_circ ? or_circ->p_circ_id : circ->n_circ_id);
    return -1;
  }

  switch (subcirc->state) {
    case SUBCIRC_STATE_PENDING_COOKIE:
    case SUBCIRC_STATE_PENDING_JOIN:
      if (!split_data->split_data_client ||
          !smartlist_contains
              (split_data->split_data_client->pending_subcircs,
               subcirc)) {
        log_warn(LD_CIRC, "split_data %p doesn't contain subcirc of circ %p "
                 "with state SUBCIRC_STATE_PENDING_* as pending subcirc",
                 split_data, or_circ ? (void*)or_circ : (void*)origin_circ);
        return -1;
      }

      if (subcirc_list_contains(split_data->subcircs, subcirc)) {
        log_warn(LD_CIRC, "split_data %p contains subcirc of circ %p "
                 "with state SUBCIRC_STATE_PENDING_* as added subcirc",
                 split_data, or_circ ? (void*)or_circ : (void*)origin_circ);
         return -1;
      }

      /* circ is pending subcirc of split_data */
      return 1;

    case SUBCIRC_STATE_ADDED:
      if (split_data_get_subcirc(split_data, subcirc->id) != subcirc) {
        log_warn(LD_CIRC, "subcirc->circ %u associated with circ %p (ID %u), "
                 "but subcirc not associated with circ's split_data",
                 subcirc->id,
                 or_circ ? (void*)or_circ : (void*)origin_circ,
                 or_circ ? or_circ->p_circ_id : circ->n_circ_id);
        return -1;
      }

      if (split_data->split_data_client &&
          smartlist_contains(split_data->split_data_client->pending_subcircs,
                             subcirc)) {
        log_warn(LD_CIRC, "split_data %p contains subcirc of circ %p with "
                 "state SUBCIRC_STATE_ADDED both as added and pending",
                 split_data, or_circ ? (void*)or_circ : (void*)origin_circ);
        return -1;
      }

      if (subcirc->id == 0 && circ != split_data->base) {
        log_warn(LD_CIRC, "split_data %p does not have base at index 0",
                 split_data);
        return -1;
      }

      if (circ == split_data->base && subcirc->id != 0) {
        log_warn(LD_CIRC, "base of split_data %p does not have index 0 "
                 "index: %u", split_data, subcirc->id);
        return -1;
      }

      /* circ has been added to split_data */
      return 0;

    case SUBCIRC_STATE_UNSPEC:
    default:
      return -1;
  }
}

/** Mark all sub-circuits associated with <b>split_data</b> for close.
 */
static void
split_data_mark_for_close(split_data_t* split_data, int reason)
{
  subcirc_list_t* subcircs;

  if (!split_data || split_data->marked_for_close)
    return;

  split_data->marked_for_close = 1;

  /* mark all sub-circuits for close */

  subcircs = split_data->subcircs;
  for (subcirc_id_t id = 0; (int)id <= subcircs->max_index; id++) {
    subcircuit_t* sub = subcirc_list_get(subcircs, id);

    if (sub) {
      tor_assert(sub->state == SUBCIRC_STATE_ADDED);
      tor_assert(sub->circ);
      if (!sub->circ->marked_for_close) {
        circuit_mark_for_close(sub->circ, reason);
      }
    }
  } /* end for */

  if (split_data->split_data_client) {
    smartlist_t* list = split_data->split_data_client->pending_subcircs;
    tor_assert(list);
    SMARTLIST_FOREACH_BEGIN(list, subcircuit_t*, sub) {
      tor_assert(sub);
      tor_assert(sub->circ);
      if (!sub->circ->marked_for_close) {
        circuit_mark_for_close(sub->circ, reason);
      }
    } SMARTLIST_FOREACH_END(sub);
  }
}

/** Remove the sub-circuit referenced by <b>subcirc_ptr</b> from
 * the split_data structure referenced by <b>split_data_ptr</b>.
 * Subsequently free the no longer needed subcircuit_t and also
 * free split_data, if no more sub-circuits are associated with it.
 * (The parameter <b>at_exit</b> is used to indicate that the function
 * was called at the end of Tor's runtime.)
 */
void
split_data_remove_subcirc(split_data_t** split_data_ptr,
                          subcircuit_t** subcirc_ptr,
                          int at_exit)
{
  split_data_t* split_data;
  subcircuit_t* subcirc;

  tor_assert(split_data_ptr);
  tor_assert(subcirc_ptr);

  split_data = *split_data_ptr;
  subcirc = *subcirc_ptr;

  tor_assert(split_data);
  tor_assert(subcirc);

  switch (subcirc->state) {
    case SUBCIRC_STATE_PENDING_COOKIE:
    case SUBCIRC_STATE_PENDING_JOIN:
      tor_assert(split_data->split_data_client);
      tor_assert(split_data->split_data_client->pending_subcircs);
      tor_assert(smartlist_contains(
          split_data->split_data_client->pending_subcircs,
          subcirc));
      smartlist_remove(
          split_data->split_data_client->pending_subcircs,
          subcirc);
      break;

    case SUBCIRC_STATE_ADDED:
      tor_assert(split_data_get_subcirc(split_data, subcirc->id) == subcirc);
      subcirc_list_remove(split_data->subcircs, subcirc->id);
      break;

    case SUBCIRC_STATE_UNSPEC:
    default:
      /* no-op */
      break;
  }

  if (subcirc->circ == split_data->base) {
    if (!at_exit) {
      split_data_mark_for_close(split_data, END_CIRC_REASON_INTERNAL);
    }

    if (CIRCUIT_IS_ORIGIN(split_data->base)) {
      origin_circuit_t* origin_base = TO_ORIGIN_CIRCUIT(split_data->base);
      tor_assert(origin_base->split_data_circuit);
      origin_base->split_data_circuit->num_split_data -= 1;

      if (origin_base->split_data_circuit->num_split_data == 0)
        split_data_circuit_free(origin_base->split_data_circuit);
    }
    split_data->base = NULL;
  }

  subcircuit_free(*subcirc_ptr);

  if (split_data_get_num_subcircs(split_data) == 0) {
    /* split_data no longer needed */
    split_data_free(*split_data_ptr);
  } else {
    /* only delete pointer to split_data */
    *split_data_ptr = NULL;
  }
}

/** For a given <b>split_data</b> return the sub-circuit that should be
 * used next for <b>direction</b>. Always return the same sub-circuit, until
 * split_data_used_subcirc was called.
 * Return NULL, if there is no active split instruction.
 */
subcircuit_t*
split_data_get_next_subcirc(split_data_t* split_data,
                            cell_direction_t direction)
{
  subcircuit_t** next_subcirc;
  split_instruction_t** instruction;
  split_instruction_t* prev;
  subcirc_id_t next_id;
  tor_assert(split_data);

  if (split_data->marked_for_close) {
    /* split_data was marked for close, but there might be pathbias testing
     * on the base circuit, so always try to return the base circuit (which
     * always has sub-circuit ID 0
     * (If the base circuit is not added anymore, NULL is returned which is
     * no problem, because then we anyway cannot use the split circuit
     * any more. */
    log_info(LD_CIRC, "split_data %p was already marked for close, returning "
             "base sub-circuit (if still added).", split_data);
    return subcirc_list_get(split_data->subcircs, 0);
  }

  switch (direction) {
    case CELL_DIRECTION_IN:
      next_subcirc = &split_data->next_subcirc_in;
      instruction = &split_data->instruction_in;
      break;
    case CELL_DIRECTION_OUT:
      next_subcirc = &split_data->next_subcirc_out;
      instruction = &split_data->instruction_out;
      break;
    default:
      tor_assert_unreached();
  }
  if (*next_subcirc)
    return *next_subcirc;

  if (!*instruction) {
    return NULL;
  }
  prev = *instruction;
  next_id = split_instruction_get_next_id(instruction);
 
  if (*instruction != prev && split_data->split_data_client){
    /* a split instruction was consumed completely and we're at the client;
     * thus, generate and send a new one */
    /* wdlc: Wait!!! only generate a full new one if this is a new page load, 
       otherwise the to-sent strategy  **MUST** be identic to the previous one
       this is particularly needed for BWR and WR, since the method establishes the usage
       of a **SINGLE** vector of dirichlet-drawn probabilities per webpage load
     */
    log_info(LD_CIRC, "Current connection status %i", split_data->split_data_client->is_final);

    log_info(LD_CIRC, "Used a split strategy in %s direction. Generate and "
             "send a new one.",
             direction == CELL_DIRECTION_OUT ? "forward" : "backward");
    split_data_generate_instruction(split_data, direction);
  }
  *next_subcirc = subcirc_list_get(split_data->subcircs, next_id);

  tor_assert(*next_subcirc);
  tor_assert((*next_subcirc)->circ);
  return *next_subcirc;
}

/** The subcirc returned by split_data_get_next_circuit was successfully
 * used. Reset split_data->next_subcirc to NULL, so that the next call
 * of split_data_get_next_subcirc returns a new one.
 */
void
split_data_used_subcirc(split_data_t* split_data,
                        cell_direction_t direction)
{
  subcircuit_t** next_subcirc;
  tor_assert(split_data);

  switch (direction) {
    case CELL_DIRECTION_IN:
      next_subcirc = &split_data->next_subcirc_in;
      break;
    case CELL_DIRECTION_OUT:
      next_subcirc = &split_data->next_subcirc_out;
      break;
    default:
      tor_assert_unreached();
  }

  *next_subcirc = NULL;
}

/** Reset <b>split_data</b>'s cache of next sub-circuits to choose.
 */
void
split_data_reset_next_subcirc(split_data_t* split_data)
{
  tor_assert(split_data);

  split_data->next_subcirc_in = NULL;
  split_data->next_subcirc_out = NULL;
}

/** Allocate a new split_data_client_t structure and return a pointer
 * (never returns NULL)
 */
split_data_client_t*
split_data_client_new(void)
{
  split_data_client_t* split_data_client;
  split_data_client = tor_malloc_zero(sizeof(split_data_client_t));

  /* initialisation of struct members */
  split_data_client->pending_subcircs = smartlist_new();
  split_data_client->strategy = split_get_default_strategy();

  return split_data_client;
}

/** Initialise a given <b>split_data_client</b> for the combination
 * (<b>base</b>, <b>middle</b>).
 */
void
split_data_client_init(split_data_client_t* split_data_client,
                       origin_circuit_t* base, crypt_path_t* middle)
{
  crypt_path_t* cpath;
  crypt_path_t* new;

  tor_assert(split_data_client);
  tor_assert(base);
  tor_assert(middle);

  split_data_client->middle_info = extend_info_dup(middle->extend_info);

  /* duplicate the important cpath information that comes after middle to
   * split_data_client->remaining_cpath
   */
  cpath = middle->next;
  do {
    tor_assert(cpath != middle);
    tor_assert(cpath);
    tor_assert(cpath->state == CPATH_STATE_OPEN);

    if (!cpath->crypto.ref_count) {
      cpath->crypto.ref_count = tor_malloc_zero(sizeof(int));
      *cpath->crypto.ref_count = 1;
    }

    new = tor_malloc_zero(sizeof(crypt_path_t));
    new->magic = CRYPT_PATH_MAGIC;
    tor_assert(cpath->state == CPATH_STATE_OPEN);
    new->state = CPATH_STATE_OPEN;
    new->extend_info = extend_info_dup(cpath->extend_info);
    memcpy(&new->crypto, &cpath->crypto, sizeof(relay_crypto_t));
    tor_assert(new->crypto.ref_count);
    *new->crypto.ref_count += 1;

    onion_append_to_cpath(&split_data_client->remaining_cpath, new);

    cpath = cpath->next;
  } while (cpath != base->cpath);
}

/** Deallocate the memory associated with <b>split_data_client</b>
 */
void
split_data_client_free_(split_data_client_t* split_data_client)
{
  if (!split_data_client)
    return;

  /* free struct members */
  tor_assert_nonfatal(!smartlist_len(split_data_client->pending_subcircs));
  smartlist_free(split_data_client->pending_subcircs);

  extend_info_free(split_data_client->middle_info);

  if (split_data_client->remaining_cpath) {
    crypt_path_t *cpath, *victim;
    cpath = split_data_client->remaining_cpath;

    while (cpath->next && cpath->next != split_data_client->remaining_cpath) {
      victim = cpath;
      cpath = victim->next;
      circuit_free_cpath_node(victim);
    }

    circuit_free_cpath_node(cpath);
    split_data_client->remaining_cpath = NULL;
  }

  tor_free(split_data_client);
}

/** Allocate a new split_data_or_t structure and return a pointer
 * (never returns NULL)
 */
split_data_or_t*
split_data_or_new(void)
{
  split_data_or_t* split_data_or;
  split_data_or = tor_malloc_zero(sizeof(split_data_or_t));

  /* initialisation of struct members */

  return split_data_or;
}

/** Initialise a given <b>split_data_or</b> by adding reference
 * to the corresponding <b>split_data</b> structure.
 */
void
split_data_or_init(split_data_or_t* split_data_or, split_data_t* split_data,
                   or_circuit_t* base)
{
  tor_assert(split_data_or);
  tor_assert(base);

  split_data_or->split_data = split_data;
  split_data_or->remaining_relay_early_cells =
      base->remaining_relay_early_cells;
}

/** Deallocate the memory associated with <b>split_data_or</b>
 */
void
split_data_or_free_(split_data_or_t* split_data_or)
{
  if (!split_data_or)
    return;

  /* free struct members */

  /* remove split_data's cookie from the split_data_or_cookie_map
   * to prevent dangling pointers (see splitor.c) */
  split_data_cookie_make_invalid(split_data_or->split_data);

  tor_free(split_data_or);
}

/** Allocate a new split_data_circuit_t structure and return a pointer
 * (never returns NULL)
 */
split_data_circuit_t*
split_data_circuit_new(void)
{
  split_data_circuit_t* split_data_circuit;
  split_data_circuit = tor_malloc_zero(sizeof(split_data_circuit_t));

  /* initialisation of struct members */

  return split_data_circuit;
}

/** Deallocate the memory associated with <b>split_data_circuit</b>
 */
void
split_data_circuit_free_(split_data_circuit_t* split_data_circuit)
{
  if (!split_data_circuit)
    return;

  log_info(LD_CIRC, "split_data_circuit %p was freed", split_data_circuit);

  /* free struct members */

  tor_free(split_data_circuit);
}

/** Allocate a new subcircuit_t structure and return a pointer
 * (never returns NULL)
 */
subcircuit_t*
subcircuit_new(void)
{
  subcircuit_t* subcirc;
  subcirc = tor_malloc_zero(sizeof(subcircuit_t));

  /* initialisation of struct members */
  subcirc->state = SUBCIRC_STATE_UNSPEC;

  subcirc->cell_buf = cell_buffer_new();
  cell_buffer_init(subcirc->cell_buf);

  return subcirc;
}

/** Deallocate the memory associated with <b>subcirc</b>
 */
void
subcircuit_free_(subcircuit_t* subcirc)
{
  if (!subcirc)
    return;

  cell_buffer_free(subcirc->cell_buf);

  tor_free(subcirc);
}

/** Return a string representation of the given sub-circuit <b>state</b>
 */
const char*
subcirc_state_str(subcirc_state_t state)
{
  switch (state) {
    case SUBCIRC_STATE_UNSPEC:
      return "SUBCIRC_STATE_UNSPEC";
    case SUBCIRC_STATE_PENDING_COOKIE:
      return "SUBCIRC_STATE_PENDING_COOKIE";
    case SUBCIRC_STATE_PENDING_JOIN:
      return "SUBCIRC_STATE_PENDING_JOIN";
    case SUBCIRC_STATE_ADDED:
      return "SUBCIRC_STATE_ADDED";
    default:
      return "UNKNOWN_STATE";
  }
}

/** Change the state of <b>subcirc</b> to <b>new_state</b> and write a
 * log message. Don't use for changing to state _ADDED, as this would
 * also require data management operations in the corresponding split_data
 */
void
subcirc_change_state(subcircuit_t* subcirc, subcirc_state_t new_state)
{
  subcirc_state_t old_state;
  circuit_t* circ;

  tor_assert(subcirc);
  tor_assert(new_state != SUBCIRC_STATE_ADDED);

  old_state = subcirc->state;
  circ = subcirc->circ;

  log_info(LD_CIRC, "Transferring circuit %p (ID %u) from state %s to %s",
           CIRCUIT_IS_ORCIRC(circ) ? (void*)TO_OR_CIRCUIT(circ) :
               (void*)TO_ORIGIN_CIRCUIT(circ),
           CIRCUIT_IS_ORCIRC(circ) ? TO_OR_CIRCUIT(circ)->p_circ_id :
                circ->n_circ_id,
           subcirc_state_str(old_state), subcirc_state_str(new_state));

  subcirc->state = new_state;
}

/** Process a relay signaling cell for the traffic splitting module which
 * arrived via <b>circ</b>. The <b>payload</b> of the cell is assumed to be
 * <b>length</b> byte long and to not contain any relay headers.
 *
 * If <b>layer_hint</b> is defined, we are at the origin of the circuit and
 * it points to the hop that packaged the cell.
 *
 * <b>command</b> must be one of RELAY_COMMAND_SPLIT_*
 */
void
split_process_relay_cell(circuit_t* circ, crypt_path_t* layer_hint,
                         cell_t* cell, int command, size_t length,
                         const uint8_t* payload)
{
  origin_circuit_t* origin_circ = NULL;
  or_circuit_t* or_circ = NULL;
  int r = 1;

  tor_assert(circ);
  if (CIRCUIT_IS_ORCIRC(circ)) {
    or_circ = TO_OR_CIRCUIT(circ);
  } else {
    origin_circ = TO_ORIGIN_CIRCUIT(circ);
  }

  tor_assert(cell);

  switch (command) {
    case RELAY_COMMAND_SPLIT_SET_COOKIE:
      if (or_circ) {
        SPLIT_MEASURE(or_circ, split_set_cookie_recv);
        SPLIT_COPY(or_circ, split_set_cookie_frombuf, &cell->received);
        r = split_process_set_cookie(or_circ, length, payload);
      }
      break;
    case RELAY_COMMAND_SPLIT_COOKIE_SET:
      if (origin_circ) {
        SPLIT_MEASURE(origin_circ, split_cookie_set_recv);
        SPLIT_COPY(origin_circ, split_cookie_set_frombuf, &cell->received);
        r = split_process_cookie_set(origin_circ, layer_hint, length,
                                     payload);
      }
      break;
    case RELAY_COMMAND_SPLIT_JOIN:
      if (or_circ) {
        SPLIT_MEASURE(or_circ, split_join_recv);
        SPLIT_COPY(or_circ, split_join_frombuf, &cell->received);
        r = split_process_join(or_circ, length, payload);
      }
      break;
    case RELAY_COMMAND_SPLIT_JOINED:
      if (origin_circ) {
        SPLIT_MEASURE(origin_circ, split_joined_recv);
        SPLIT_COPY(origin_circ, split_joined_frombuf, &cell->received);
        r = split_process_joined(origin_circ, layer_hint, length, payload);
      }
      break;
    case RELAY_COMMAND_SPLIT_INSTRUCTION:
      if (or_circ) {
        SPLIT_MMEASURE(or_circ, split_instruction_recv,
                       SPLIT_EVAL_INSTRUCTIONS);
        SPLIT_MCOPY(or_circ, split_instruction_frombuf,
                    SPLIT_EVAL_INSTRUCTIONS, &cell->received);
        r = split_process_instruction(or_circ, length, payload,
                                      CELL_DIRECTION_IN);
      }
      break;
    case RELAY_COMMAND_SPLIT_INFO:
      if (or_circ) {
        SPLIT_MMEASURE(or_circ, split_info_recv, SPLIT_EVAL_INSTRUCTIONS);
        SPLIT_MCOPY(or_circ, split_info_frombuf, SPLIT_EVAL_INSTRUCTIONS,
                    &cell->received);
        r = split_process_instruction(or_circ, length, payload,
                                      CELL_DIRECTION_OUT);
      }
      break;
    default:
      tor_fragile_assert();
  }

  if (r == 0 && origin_circ) {
    /* This was a valid cell. Count it as delivered + overhead. */
    circuit_read_valid_data(origin_circ, length);
  } else if (r < 0) {
    log_info(LD_PROTOCOL, "Error while processing split relay cell %d. "
             "Dropping...", command);
  } else if (r == 1) {
    log_info(LD_PROTOCOL, "Relay cell %d was received at wrong node type"
             "(client/middle). Dropping...", command);
  }

  return;
}

/** Mark all split_data's associated with <b>circ</b> for close
 * (due to <b>reason</b>).
 */
void
split_mark_for_close(circuit_t *circ, int reason)
{
  tor_assert(circ);

  if (CIRCUIT_IS_ORCIRC(circ)) {
    /* we're at the merging middle */
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

    if (or_circ->split_data) {
      tor_assert(or_circ->subcirc);
      split_data_mark_for_close(or_circ->split_data, reason);
    }

  } else {
    /* we're at the client */
    origin_circuit_t* origin_circ = TO_ORIGIN_CIRCUIT(circ);
    crypt_path_t* cpath = origin_circ->cpath;

    if (!cpath)
      /* a building/unfinished circuit might not have a cpath; in this case
         we're done */
      return;

    do {
      tor_assert(cpath);
      if (cpath->split_data) {
        tor_assert(cpath->subcirc);
#ifndef SPLIT_EVAL
        /* during evaluation: abandon the whole split circuit,
         * when building of an unjoined sub-circuit fails */
        if (cpath->subcirc->state == SUBCIRC_STATE_ADDED ||
            circ == split_data_get_base(cpath->split_data, 0))
#endif /* SPLIT_EVAL */
          split_data_mark_for_close(cpath->split_data, reason);
      }
      cpath = cpath->next;
    } while (cpath != origin_circ->cpath);
  }
}

/** Remove <b>circ</b> from any split circuits it is associated with.
 * (The parameter <b>at_exit</b> is used to indicate that the function
 * was called at the end of Tor's runtime.)
 */
void
split_remove_subcirc(circuit_t* circ, int at_exit)
{
  tor_assert(circ);

  if (CIRCUIT_IS_ORCIRC(circ)) {
    /* we're at the merging middle */
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

    if (or_circ->split_data) {
      //DEBUG-split
      int r = split_data_check_subcirc(or_circ->split_data, circ);
      /* circ must be either added to split data or pending */
      tor_assert_nonfatal(r == 0 || r == 1);

      log_info(LD_CIRC, "Removed circ %p (ID %u) from split_data %p",
                     or_circ, or_circ->p_circ_id, or_circ->split_data);

      split_data_remove_subcirc(&or_circ->split_data, &or_circ->subcirc,
                                at_exit);
    }

    //DEBUG-split
    tor_assert(or_circ->split_data == NULL);
    tor_assert(or_circ->subcirc == NULL);

  } else {
    /* we're at the client */
    origin_circuit_t* origin_circ = TO_ORIGIN_CIRCUIT(circ);
    crypt_path_t* cpath = origin_circ->cpath;

    if (!cpath)
      /* a building/unfinished circuit might not have a cpath; in this case
         we're done */
      return;

    do {
      tor_assert(cpath);

      if (cpath->split_data) {
        //DEBUG-split
        int r = split_data_check_subcirc(cpath->split_data, circ);
        /* circ must be either added to split data or pending */
        tor_assert_nonfatal(r == 0 || r == 1);

        log_info(LD_CIRC, "Removed circ %p (ID %u) from split_data %p",
                 origin_circ, circ->n_circ_id, cpath->split_data);

        split_data_remove_subcirc(&cpath->split_data, &cpath->subcirc,
                                  at_exit);
      }
      //DEBUG-split
      tor_assert(cpath->split_data == NULL);
      tor_assert(cpath->subcirc == NULL);
      cpath = cpath->next;
    } while (cpath != origin_circ->cpath);
  }
}

/** Check, if a split circuit must be obeyed for handling the given
 * <b>circ</b> (at <b>layer_hint</b>, if applicable).
 * If yes, return the base of that split circuit; otherwise return
 * NULL.
 */
circuit_t*
split_is_relevant(circuit_t* circ, crypt_path_t* layer_hint)
{
  circuit_t* base = NULL;
  tor_assert(circ);

  if (CIRCUIT_IS_ORIGIN(circ)) {
    crypt_path_t* cpath = TO_ORIGIN_CIRCUIT(circ)->cpath;
    tor_assert(layer_hint);

    /* search for split_datas on the path that we need to obey */
    do {
      tor_assert(cpath);

      if (cpath == layer_hint)
        /* stop the search */
        break;

      if (cpath->split_data) {
        tor_assert(cpath->subcirc);

        if (base)
          /* DEUBG-split all cpaths of a circ must have the same base */
          tor_assert(base == split_data_get_base(cpath->split_data, 0));
        else if (cpath->subcirc->state == SUBCIRC_STATE_ADDED) {
          tor_assert(split_data_check_subcirc(cpath->split_data, circ) == 0);
          base = split_data_get_base(cpath->split_data, 1);
        }
      }

      cpath = cpath->next;
    } while (cpath != TO_ORIGIN_CIRCUIT(circ)->cpath);

    tor_assert(cpath == layer_hint);

  } else { /* CIRCUIT_IS_ORIGIN(circ) */
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

    if (or_circ->split_data) {
      tor_assert(or_circ->subcirc);
      if (or_circ->subcirc->state == SUBCIRC_STATE_ADDED) {
        base = split_data_get_base(or_circ->split_data, 1);
      }
    }
  }

  if (!base) {
    /* there is no relevant split circuit for (circ, layer_hint) */
    return NULL;
  }

  log_debug(LD_CIRC, "circ %p is relevant: found base %p",
            CIRCUIT_IS_ORIGIN(circ) ? (void*)TO_ORIGIN_CIRCUIT(circ) :
               (void*)TO_OR_CIRCUIT(circ),
            CIRCUIT_IS_ORIGIN(base) ? (void*)TO_ORIGIN_CIRCUIT(base) :
                (void*)TO_OR_CIRCUIT(base));

  return base;
}

/** Return the cpath layer of <b>new_circ</b> that points to the exact
 * same node as <b>old_cpath_layer</b>. Assumes that such a cpath layer
 * exists for new_circ.
 */
crypt_path_t*
split_find_equal_cpath(circuit_t* new_circ,
                       crypt_path_t* old_cpath_layer)
{
  origin_circuit_t* origin_circ;
  crypt_path_t* cpath;
  tor_assert(new_circ);
  tor_assert(old_cpath_layer);

  if (!CIRCUIT_IS_ORIGIN(new_circ))
    return old_cpath_layer;

  origin_circ = TO_ORIGIN_CIRCUIT(new_circ);
  tor_assert(origin_circ->cpath);
  cpath = origin_circ->cpath->prev;
  do {
    tor_assert(cpath);
    if (compare_digests(old_cpath_layer->extend_info->identity_digest,
                        cpath->extend_info->identity_digest))
      return cpath;

    cpath = cpath->prev;
  } while (cpath != origin_circ->cpath->prev);

  tor_assert_unreached();
  return NULL;
}

/** For a given <b>circ</b> that is part of a split circuit, return
 * the base circuit of that split circuit.
 * Return NULL, if circ is not part of a split circuit.
 */
circuit_t*
split_get_base_(circuit_t* circ)
{
  circuit_t* base = NULL;
  tor_assert(circ);

  if (CIRCUIT_IS_ORCIRC(circ)) {
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

    if (or_circ->split_data) {
      tor_assert(or_circ->subcirc);
      if (or_circ->subcirc->state == SUBCIRC_STATE_ADDED)
        base = split_data_get_base(TO_OR_CIRCUIT(circ)->split_data, 1);
    }
  } else {
    crypt_path_t* cpath = TO_ORIGIN_CIRCUIT(circ)->cpath;

    do {
      tor_assert(cpath);

      if (cpath->split_data) {
        tor_assert(cpath->subcirc);

        if (base)
          /* DEUBG-split all cpaths of a circ must have the same base */
          tor_assert(base == split_data_get_base(cpath->split_data, 0));
        else if (cpath->subcirc->state == SUBCIRC_STATE_ADDED) {
          tor_assert(split_data_check_subcirc(cpath->split_data, circ) == 0);
          base = split_data_get_base(cpath->split_data, 1);
        }
      }

      cpath = cpath->next;
    } while (cpath != TO_ORIGIN_CIRCUIT(circ)->cpath);
  }

  return base;
}

/** Return the base circuit of the split circuit that the given
 * <b>circ</b> is a relevant part of.
 * If the given circ is not part of any split circuit, simply
 * return circ.
 */
circuit_t*
split_get_base(circuit_t* circ)
{
  circuit_t* base;
  tor_assert(circ);
  base = split_get_base_(circ);

  if (!base)
    base = circ;

  return base;
}

/** For a given pair of a split <b>base</b> circuit and source/destination
 * <b>layer_hint</b>, return the middle (of base) that should be used next
 * for <b>direction</b>. Always return the same middle node, until
 * split_base_used_middle was called.
 */
static crypt_path_t*
split_base_get_next_middle(origin_circuit_t* base, crypt_path_t* layer_hint,
                           cell_direction_t direction)
{
  /* TODO-split for now returns the first middle node it can find where
   * split_data is present. In future, provide integration into an overall
   * splitting strategy */

  crypt_path_t** next_middle;
  crypt_path_t* cpath;

  tor_assert(base);
  tor_assert(layer_hint);

  tor_assert(base->split_data_circuit);
  switch (direction) {
    case CELL_DIRECTION_IN:
      next_middle = &base->split_data_circuit->next_middle_in;
      break;
    case CELL_DIRECTION_OUT:
      next_middle = &base->split_data_circuit->next_middle_out;
      break;
    default:
      tor_assert_unreached();
  }

  if (*next_middle)
    return *next_middle;

  /* select next middle */
  cpath = base->cpath;
  do {
    tor_assert(cpath);
    if (cpath == layer_hint)
      break;

    if (cpath->split_data) {
      *next_middle = cpath;
      break;
    }

    cpath = cpath->next;
  } while (cpath != base->cpath);

  tor_assert(*next_middle);
  return *next_middle;
}

/** The cpath returned by split_get_next_middle was successfully
 * used. Ensure that a new cpath is returned by the next call to
 * split_get_next_middle.
 */
static void
split_base_used_middle(origin_circuit_t* base, cell_direction_t direction)
{
  crypt_path_t** next_middle;
  tor_assert(base);

  tor_assert(base->split_data_circuit);
  switch (direction) {
    case CELL_DIRECTION_IN:
      next_middle = &base->split_data_circuit->next_middle_in;
      break;
    case CELL_DIRECTION_OUT:
      next_middle = &base->split_data_circuit->next_middle_out;
      break;
    default:
      tor_assert_unreached();
  }

  if (*next_middle)
    split_data_used_subcirc((*next_middle)->split_data, direction);

  *next_middle = NULL;
}

/** Return the sub-circuit that should be used next in <b>direction</b>
 * according to the splitting strategy of the split circuit defined
 * through <b>base</b> and <b>dest</b>.
 * Assumes that base is, in fact, the base of a split circuit
 * and that dest is part of base's cpath.
 * Returns NULL, if there is no active split instruction.
 */
subcircuit_t*
split_get_next_subcirc(circuit_t* base, crypt_path_t* dest,
                       cell_direction_t direction)
{
  split_data_t* split_data;
  subcircuit_t* subcirc;
  tor_assert(base);

  split_data = split_get_next_split_data(base, dest, direction);

  tor_assert(split_data);

  subcirc = split_data_get_next_subcirc(split_data, direction);

  return subcirc;
}

/** Return the split_data that is to be used next by the split circuit
 * defined through <b>base</b> when communicating with <b>dest</b>
 * (dest = NULL for or_circuits).
 * Assumes that base is, in fact, the base of a split circuit.
 */
split_data_t*
split_get_next_split_data(circuit_t* base, crypt_path_t* dest,
                          cell_direction_t direction)
{
  split_data_t* split_data;
  tor_assert(base);

  if (CIRCUIT_IS_ORIGIN(base)) {
    origin_circuit_t* origin_circ = TO_ORIGIN_CIRCUIT(base);
    crypt_path_t* next_middle;

    next_middle = split_base_get_next_middle(origin_circ, dest, direction);
    split_data = next_middle->split_data;
  } else {
    split_data = TO_OR_CIRCUIT(base)->split_data;
  }

  tor_assert(split_data);
  return split_data;
}

/** The circuit returned by split_get_next_circuit was successfully
 * used. Ensure that a new circuit is returned by the next call to
 * split_get_next_circuit.
 * Do nothing, if base == NULL.
 */
void
split_used_circuit(circuit_t* base, cell_direction_t direction)
{
  if (!base)
    return;

  if (CIRCUIT_IS_ORCIRC(base)) {
    split_data_used_subcirc(TO_OR_CIRCUIT(base)->split_data, direction);
  } else {
    split_base_used_middle(TO_ORIGIN_CIRCUIT(base), direction);
  }
}

void
split_base_inc_blocked(circuit_t* base)
{
  origin_circuit_t* origin_base;
  tor_assert(base);

  origin_base = TO_ORIGIN_CIRCUIT(base);
  tor_assert(origin_base->split_data_circuit);

  origin_base->split_data_circuit->num_blocked += 1;
}

void
split_base_dec_blocked(circuit_t* base)
{
  origin_circuit_t* origin_base;
  tor_assert(base);

  origin_base = TO_ORIGIN_CIRCUIT(base);
  tor_assert(origin_base->split_data_circuit);

  if (origin_base->split_data_circuit->num_blocked > 0)
    origin_base->split_data_circuit->num_blocked -= 1;
}

int
split_base_should_unblock(circuit_t* base)
{
  origin_circuit_t* origin_base;
  tor_assert(base);

  origin_base = TO_ORIGIN_CIRCUIT(base);
  tor_assert(origin_base->split_data_circuit);

  return origin_base->split_data_circuit->num_blocked == 0;
}

/** Store <b>cell</b> in <b>subcirc</b>'s split_cell_buf for later
 * reordering
 */
void
split_buffer_cell(subcircuit_t* subcirc, cell_t* cell)
{
  cell_buffer_t* buf = NULL;
  tor_assert(subcirc);
  tor_assert(cell);

  /* Check and run the OOM if needed. */
  if (PREDICT_UNLIKELY(cell_queues_check_size())) {
    /* We ran the OOM handler which might have closed this circuit. */
    if (subcirc->circ->marked_for_close)
      return;
  }

  buf = subcirc->cell_buf;

  tor_assert(buf);
  cell_buffer_append_cell(buf, cell);
}

/** Handle cells that were potentially buffered while we were waiting for the
 * split cell that just arrived on <b>circ</b> from <b>layer_hint</b>.
 * (layer_hint is NULL, if we are at the or/middle)
 */
void
split_handle_buffered_cells(circuit_t* circ)
{
  circuit_t* base;
  subcircuit_t* next_subcirc;
  buffered_cell_t* buf_cell;
  tor_assert(circ);

  base = split_get_base_(circ);
  if (!base)
    /* circ is no split circuit */
    return;

  if (CIRCUIT_IS_ORIGIN(circ)) {
    tor_assert(CIRCUIT_IS_ORIGIN(base)); //DEBUG-split
    crypt_path_t* cpath = TO_ORIGIN_CIRCUIT(base)->cpath;

    do {
      tor_assert(cpath);

      if (cpath->split_data) {
        next_subcirc = split_data_get_next_subcirc(cpath->split_data,
                                                   CELL_DIRECTION_IN);

        while (next_subcirc && next_subcirc->cell_buf->num > 0) {
          int reason;
          buf_cell = cell_buffer_pop(next_subcirc->cell_buf);
          tor_assert(buf_cell);

          tor_assert(cpath->next != cpath);
          tor_assert(cpath->next != TO_ORIGIN_CIRCUIT(base)->cpath);

          if ((reason = circuit_receive_relay_cell_impl(&buf_cell->cell, base,
                CELL_DIRECTION_IN, cpath->next)) < 0) {
            log_warn(LD_CIRC,"circuit_receive_relay_cell backward failed. "
                     "Closing.");
            /* Always emit a bandwidth event for closed circs */
            if (CIRCUIT_IS_ORIGIN(base)) {
              control_event_circ_bandwidth_used_for_circ(TO_ORIGIN_CIRCUIT(base));
            }
            circuit_mark_for_close(base, -reason);
          }

          buffered_cell_free(buf_cell);
          split_data_used_subcirc(cpath->split_data, CELL_DIRECTION_IN);
          next_subcirc = split_data_get_next_subcirc(cpath->split_data,
                                                     CELL_DIRECTION_IN);
        }

        if (!next_subcirc) {
          log_info(LD_CIRC, "Cannot handle buffered split cells for "
                   "split_data %p, as there is no active split instruction",
                   cpath->split_data);
        }
      }

      cpath = cpath->next;
    } while (cpath != TO_ORIGIN_CIRCUIT(base)->cpath);

  } else {
    tor_assert(CIRCUIT_IS_ORCIRC(base)); //DEBUG-split

    next_subcirc = split_get_next_subcirc(base, NULL, CELL_DIRECTION_OUT);

    while (next_subcirc && next_subcirc->cell_buf->num > 0) {
      buf_cell = cell_buffer_pop(next_subcirc->cell_buf);
      tor_assert(buf_cell);

      //TODO-split add rendezvous-splice
      tor_assert(base->n_chan);

      log_debug(LD_OR, "Passing on buffered split cell.");

      stats_n_relay_cells_relayed++;
      append_cell_to_circuit_queue(base, base->n_chan, &buf_cell->cell,
                                   CELL_DIRECTION_OUT, 0);

#ifdef SPLIT_EVAL_DATARATE
  if (CIRCUIT_IS_ORCIRC(circ)) {
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);
    if (or_circ->split_eval_data.consider) {
      split_eval_append_cell(&or_circ->split_eval_data, CELL_DIRECTION_OUT,
                             &buf_cell->cell.received, &base->temp);
    }
  }
#endif /* SPLIT_EVAL_DATARATE */

      buffered_cell_free(buf_cell);
      split_used_circuit(base, CELL_DIRECTION_OUT);
      next_subcirc = split_get_next_subcirc(base, NULL, CELL_DIRECTION_OUT);
    }

    if (!next_subcirc)
      log_info(LD_CIRC, "Cannot handle buffered split cells for "
               "split_data %p, as there is no active split instruction",
               TO_OR_CIRCUIT(base)->split_data);
  }
}

/** Return the age of the oldest buffered split cell of <b>circ</b> in
 * timestamp units as measured from <b>now</b>. Return 0, if circ is no
 * split circuit or if no cells are buffered.
 *
 * This function will return incorrect results if the oldest cell buffered
 * on the circuit is older than about 2**32 msec (about 49 days) old.
 */
uint32_t
split_max_buffered_cell_age(const circuit_t* circ, uint32_t now)
{
  uint32_t age = 0, tmp_age;
  tor_assert(circ);

  if (CIRCUIT_IS_ORIGIN(circ)) {
    crypt_path_t* cpath = CONST_TO_ORIGIN_CIRCUIT(circ)->cpath;

    do {
      tor_assert(cpath);

      if (cpath->subcirc) {
        tmp_age = cell_buffer_max_buffered_age(cpath->subcirc->cell_buf, now);
        if (tmp_age > age)
          age = tmp_age;
      }

      cpath = cpath->next;
    } while (cpath != CONST_TO_ORIGIN_CIRCUIT(circ)->cpath);
  } else {
    const or_circuit_t* or_circ = CONST_TO_OR_CIRCUIT(circ);

    if (or_circ->subcirc) {
      age = cell_buffer_max_buffered_age(or_circ->subcirc->cell_buf, now);
    }
  }

  return age;
}

/* For a given <b>circ</b> that was marked for close, free all associated
 * split buffers
 */
size_t
split_marked_circuit_free_buffer(circuit_t* circ)
{
  size_t freed = 0;
  tor_assert(circ);

  if (BUG(!circ->marked_for_close))
    return 0;

  if (CIRCUIT_IS_ORIGIN(circ)) {
      crypt_path_t* cpath = TO_ORIGIN_CIRCUIT(circ)->cpath;

      do {
        tor_assert(cpath);

        if (cpath->subcirc)
          freed += cell_buffer_clear(cpath->subcirc->cell_buf);

        cpath = cpath->next;
      } while (cpath != TO_ORIGIN_CIRCUIT(circ)->cpath);
    } else {
      or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

      if (or_circ->subcirc) {
        freed += cell_buffer_clear(or_circ->subcirc->cell_buf);
      }
    }

  return freed;
}

