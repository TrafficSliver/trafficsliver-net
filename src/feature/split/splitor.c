/**
 * \file splitor.c
 *
 * \brief Traffic splitting implementation: code used by the OR (middle)
 **/

#define MODULE_SPLIT_INTERNAL
#include "feature/split/splitor.h"

#include "core/or/or.h"
#include "core/or/circuitlist.h"
#include "core/or/relay.h"
#include "core/or/cell_st.h"
#include "core/or/or_circuit_st.h"
#include "ext/ht.h"
#include "feature/split/splitcommon.h"
#include "feature/split/splitdefines.h"
#include "feature/split/splitutil.h"

#include <string.h>

/** Map of all currently valid split cookies to their corresponding
 * split_data_or_t's (which in turn point to the actual split_data_t's)
 */
static HT_HEAD(split_data_or_cookie_ht, split_data_or_t)
      split_data_or_cookie_map = HT_INITIALIZER();

/** Check, if the or_circuit <b>circ</b> should be used for split circuits.
 * Return -1, if check fails; otherwise 0.
 */
static int
split_check_or_circuit(or_circuit_t* circ)
{
  if (!circ) {
      return -1;
    }

    if (TO_CIRCUIT(circ)->marked_for_close) {
      log_warn(LD_CIRC, "Circuit %p (ID %u) is marked for close; don't use as "
               "split circuit", circ, circ->p_circ_id);
      return -1;
    }

    if (TO_CIRCUIT(circ)->state != CIRCUIT_STATE_OPEN) {
      log_warn(LD_CIRC, "Circuit %p (ID %u) is not open; don't use as "
               "split circuit (current state: %s)", circ,
               TO_CIRCUIT(circ)->n_circ_id,
               circuit_state_to_string(TO_CIRCUIT(circ)->state));
      return -1;
    }

    if (TO_CIRCUIT(circ)->purpose != CIRCUIT_PURPOSE_OR) {
      log_warn(LD_CIRC, "Circuit %p (ID %u) is of purpose '%s'; don't use as "
               "split circuit", circ, TO_CIRCUIT(circ)->n_circ_id,
               circuit_purpose_to_string(TO_CIRCUIT(circ)->purpose));
      return -1;
    }

    //TODO-split further checks ?

    return 0;
}

/** Send a COOKIE_SET cell towards client via circuit <b>circ</b>. If
 * <b>success</b> is TRUE, this cell contains the payload |0x01|<b>id</b>|.
 * Otherwise, it contains the payload |0x00|.
 * Return -1, if sending fails; otherwise 0.
 */
static int
split_send_cookie_response(or_circuit_t* circ, subcirc_id_t id,
                           uint8_t success)
{
  char* payload;
  size_t length;
  int retval;

  tor_assert(circ);

  length = 1;
  if (success)
    length += sizeof(subcirc_id_t);

  payload = tor_malloc_zero(length);

  if (success) {
    payload[0] = 0x01;
    write_subcirc_id(subcirc_id_hton(id), (payload + 1));
  } else {
    payload[0] = 0x00;
  }

  log_info(LD_CIRC, "Sending COOKIE_SET %s cell to circ %p (ID %u); "
           "payload: %s", success ? "success" : "error",
           circ, circ->p_circ_id, hex_str(payload, length));

  retval = relay_send_command_from_edge(0, TO_CIRCUIT(circ),
                                        RELAY_COMMAND_SPLIT_COOKIE_SET,
                                        payload, length, NULL);

  tor_free(payload);
  return retval;
}

/** Send a JOINED cell towards client via circuit <b>circ</b>. If
 * <b>success</b> is TRUE, this cell contains the payload |0x01|<b>id</b>|.
 * Otherwise, the provided cookie is not valid anymore and the cell
 * contains the payload |0x00|.
 * Return -1, if sending fails; otherwise 0.
 */
static int
split_send_join_response(or_circuit_t* circ, subcirc_id_t id, int success)
{
  char* payload;
  size_t length;
  int retval;

  length = 1;
  if (success)
    length += sizeof(subcirc_id_t);

  payload = tor_malloc_zero(length);

  if (success) {
    payload[0] = 0x01;
    write_subcirc_id(subcirc_id_hton(id), (payload + 1));
  } else {
    payload[0] = 0x00;
  }

  log_info(LD_CIRC, "Sending split JOINED %s cell to circ %p (ID %u); "
           "payload: %s", success  ? "success" : "inv-cookie",
           circ, circ->p_circ_id, hex_str(payload, length));

  retval = relay_send_command_from_edge(0, TO_CIRCUIT(circ),
                                        RELAY_COMMAND_SPLIT_JOINED,
                                        payload, length, NULL);

  tor_free(payload);
  return retval;
}

/** Get and return next free sub-circuit ID, based on the sub-circuits already
 * associated with <b>split_data</b>. Assumes that sub-circuit IDs are chosen
 * by the middle node strictly in-order.
 */
static subcirc_id_t
split_get_new_subcirc_id(split_data_t* split_data)
{
  int next_id;

  next_id = split_data_get_num_subcircs(split_data);
  tor_assert(next_id < MAX_SUBCIRCS);

  return (subcirc_id_t)next_id;
}

/** Compare the associated split cookies of <b>s1</b> and <b>s2</b>
 */
static int
split_data_or_cookie_equal(const split_data_or_t* s1,
                           const split_data_or_t* s2)
{
  tor_assert(s1);
  tor_assert(s1->split_data);
  tor_assert(s2);
  tor_assert(s2->split_data);
  return tor_memeq(s1->split_data->cookie, s2->split_data->cookie,
                   SPLIT_COOKIE_LEN);
}

/** Generate a hash value for <b>s</b> based on the associated split cookie
 */
static unsigned
split_data_or_cookie_hash(const split_data_or_t* s)
{
  tor_assert(s);
  tor_assert(s->split_data);
  return (unsigned) siphash24g(s->split_data->cookie, SPLIT_COOKIE_LEN);
}

HT_PROTOTYPE(split_data_or_cookie_ht, split_data_or_t, node,
             split_data_or_cookie_hash, split_data_or_cookie_equal)
HT_GENERATE2(split_data_or_cookie_ht, split_data_or_t, node,
             split_data_or_cookie_hash, split_data_or_cookie_equal,
             0.6, tor_reallocarray_, tor_free_)

/** Set the cookie state of <b>split_data</b> to SPLIT_COOKIE_STATE_VALID
 * and add its split_data_or to split_data_or_cookie_map.
 * May only be used inside for the or/middle side!
 */
static void
split_data_cookie_make_valid(split_data_t* split_data)
{
  split_data_or_t* old_entry;

  tor_assert(split_data);
  tor_assert(split_data->split_data_or);

  split_data->cookie_state = SPLIT_COOKIE_STATE_VALID;

  old_entry = HT_REPLACE(split_data_or_cookie_ht, &split_data_or_cookie_map,
                         split_data->split_data_or);

  /* make cookie of previously stored split_data_or invalid */
  if (old_entry) {
    tor_assert(old_entry->split_data);
    old_entry->split_data->cookie_state = SPLIT_COOKIE_STATE_INVALID;
  }
}

/** Set the cookie state of <b>split_data</b> to SPLIT_COOKIE_STATE_INVALID
 * and remove its split_data_or from split_data_or_cookie_map.
 * May only be used for the or/middle side!
 */
void
split_data_cookie_make_invalid(split_data_t* split_data)
{
  tor_assert(split_data);
  tor_assert(split_data->split_data_or);

  split_data->cookie_state = SPLIT_COOKIE_STATE_INVALID;

  HT_REMOVE(split_data_or_cookie_ht, &split_data_or_cookie_map,
            split_data->split_data_or);
}

/** Find and return the split_data structure which has the given
 * <b>cookie</b> as SPLIT_COOKIE_STATE_VALID cookie.
 * Return NULL, if no such split_data structure can be found.
 * Assumes cookie to have size SPLIT_COOKIE_LEN.
 */
static split_data_t*
split_get_split_data_by_cookie(const uint8_t* cookie)
{
  split_data_t dummy_data = {};
  split_data_or_t dummy_data_or = {};
  split_data_or_t* found;

  dummy_data.split_data_or = &dummy_data_or;
  dummy_data_or.split_data = &dummy_data;

  memcpy(&dummy_data.cookie, cookie, SPLIT_COOKIE_LEN);

  found = HT_FIND(split_data_or_cookie_ht, &split_data_or_cookie_map,
                  &dummy_data_or);

  if (found) {
    tor_assert(found->split_data);
    tor_assert(found->split_data->cookie_state == SPLIT_COOKIE_STATE_VALID);
  } else {
    return NULL;
  }

  log_info(LD_CIRC, "Found split_data %p with cookie %s",
           found ? found->split_data: NULL,
           hex_str((const char*)cookie, SPLIT_COOKIE_LEN));

  return found->split_data;
}

/** Process a SET_COOKIE cell (with <b>payload</b> and <b>length</b>) that was
 * received on circuit <b>circ</b>.
 * Return -1 on failure; otherwise 0.
 */
int
split_process_set_cookie(or_circuit_t* circ, size_t length,
                         const uint8_t* payload)
{
  split_data_t* split_data;
  subcirc_id_t subcirc_id;

  tor_assert(circ);
  tor_assert(payload);

  if (length != SPLIT_COOKIE_LEN) {
    log_info(LD_CIRC, "Received SET_COOKIE cell on circuit %p (ID %u) with "
             "wrong length %u (should be %u). Dropping.", circ,
             circ->p_circ_id, (unsigned int)length, SPLIT_COOKIE_LEN);
    return -1;
  }

  log_info(LD_CIRC, "Received SET_COOKIE cell on circuit %p (ID %u) with "
           "cookie: %s", circ, circ->p_circ_id,
           hex_str((const char*)payload, length));

  if (!circ->split_data) {
    /* generate and initialise a new split_data structure */
    if (split_check_or_circuit(circ)) {
      log_warn(LD_CIRC, "Circuit %p (ID %u) not suited as split circuit. "
               "Notifying client...", circ, circ->p_circ_id);
      if (split_send_cookie_response(circ, 0, 0)) {
        log_warn(LD_CIRC, "Could not send split cookie response. Closing...");
        /* already marked for close */
        return -1;
      }
      return 0;
    }

    tor_assert(!circ->subcirc);

    split_data = split_data_new();
    split_data_init_or(split_data, circ);
    circ->split_data = split_data;

    /* add circ as sub-circuit to the new split_data structure */
    subcirc_id = split_get_new_subcirc_id(split_data);
    circ->subcirc = split_data_add_subcirc(split_data, SUBCIRC_STATE_ADDED,
                                           TO_CIRCUIT(circ), subcirc_id);

    SPLIT_MEASURE(circ, split_data_created);

    tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 0);
  } else {
    split_data = circ->split_data;
    tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 0);
    subcirc_id = circ->subcirc->id;
  }

  /* store cookie in split_data */
  split_data_cookie_make_invalid(split_data);
  memcpy(split_data->cookie, payload, SPLIT_COOKIE_LEN);
  split_data_cookie_make_valid(split_data);

  /* send back COOKIE_SET cell */
  if (split_send_cookie_response(circ, subcirc_id, 1)) {
    log_warn(LD_CIRC, "Could not send split cookie response. Closing...");
    /* already marked for close */
    return -1;
  }

  return 0;
}

/** Process a JOIN cell (with <b>payload</b> and <b>length</b>) that was
 * received on circuit <b>circ</b>.
 * Return -1 on failure; otherwise 0.
 */
int
split_process_join(or_circuit_t* circ, size_t length,
                   const uint8_t* payload)
{
  split_data_t* split_data;

  tor_assert(circ);
  tor_assert(payload);

  if (length != SPLIT_COOKIE_LEN) {
    log_info(LD_CIRC, "Received JOIN cell on circuit %p (ID %u) with "
             "wrong length %u (should be %u). Dropping.", circ,
             circ->p_circ_id, (unsigned int)length, SPLIT_COOKIE_LEN);
    return -1;
  }

  if (circ->split_data) {
    tor_assert(circ->subcirc);
    log_info(LD_CIRC, "Received JOIN cell on circuit %p (ID %u) which "
             "was already added to split_data %p with ID %u. Dropping.",
             circ, circ->p_circ_id, circ->split_data, circ->subcirc->id);
    return -1;
  }
  tor_assert(!circ->subcirc);

  log_info(LD_CIRC, "Received JOIN cell on circuit %p (ID %u) with "
            "cookie: %s", circ, circ->p_circ_id,
            hex_str((const char*)payload, length));

  split_data = split_get_split_data_by_cookie(payload);

  if (split_data) {
    /* found correct split circuit */
    subcirc_id_t subcirc_id;

    /* add circ to the found split circuit */
    circ->split_data = split_data;
    subcirc_id = split_get_new_subcirc_id(split_data);
    circ->subcirc = split_data_add_subcirc(split_data, SUBCIRC_STATE_ADDED,
                                           TO_CIRCUIT(circ), subcirc_id);

    tor_assert(split_data_check_subcirc(split_data, TO_CIRCUIT(circ)) == 0);

    /* send back JOINED cell */
    if (split_send_join_response(circ, subcirc_id, 1)) {
      log_warn(LD_CIRC, "Could not send split join response. Closing...");
      /* already marked for close */
      return -1;
    }

  } else { /* split_data */
    /* split_data not found, handle gracefully and ask for new cookie*/
    log_warn(LD_CIRC, "Requested split cookie wasn't found, might be "
             "invalid. Ask for new cookie...");
    if (split_send_join_response(circ, 0, 0)) {
       log_warn(LD_CIRC, "Could not send split join response. Closing...");
       /* already marked for close */
       return -1;
     }
  }

  return 0;
}

/** Decrease the number of remaining relay early cells for the given split
 * <b>circ</b> by one.
 */
void
split_decrease_remaining_relay_early(or_circuit_t* circ)
{
  tor_assert(circ);

  if (!circ->split_data)
    return;

  tor_assert(circ->subcirc);
  if (circ->subcirc->state != SUBCIRC_STATE_ADDED)
    return;

  tor_assert(circ->split_data->split_data_or);
  if (circ->split_data->split_data_or->remaining_relay_early_cells > 0)
    circ->split_data->split_data_or->remaining_relay_early_cells -= 1;
}

/** Rewrite a RELAY_EARLY <b>cell</b> received on a split <b>circ</b> to be a
 * plain RELAY cell, if too many RELAY_EARLY cells have already been forwarded
 * on that split circuit.
 */
void
split_rewrite_relay_early(or_circuit_t* circ, cell_t* cell)
{
  tor_assert(cell);
  tor_assert(circ);

  if (cell->command != CELL_RELAY_EARLY)
    return;

  if (!circ->split_data)
    return;

  tor_assert(circ->subcirc);
  tor_assert(circ->subcirc->state == SUBCIRC_STATE_ADDED);

  tor_assert(circ->split_data->split_data_or);
  if (circ->split_data->split_data_or->remaining_relay_early_cells > 0)
    return;

  cell->command = CELL_RELAY;
}

/** Process an split instructions cell (with <b>payload</b> and <b>length</b>)
 * that was received on circuit <b>circ</b>.
 * Return -1 on failure; otherwise 0.
 */
int
split_process_instruction(or_circuit_t* circ, size_t length,
                          const uint8_t* payload, cell_direction_t direction)
{
  split_data_t* split_data;
  split_instruction_t** existing_instructions;
  split_instruction_t* received;

  tor_assert(circ);
  tor_assert(payload);

  split_data = circ->split_data;
  tor_assert(split_data);

  switch (direction) {
    case CELL_DIRECTION_IN:
      existing_instructions = &split_data->instruction_in;
      break;
    case CELL_DIRECTION_OUT:
      existing_instructions = &split_data->instruction_out;
      break;
    default:
      tor_assert_unreached();
  }

  received = split_payload_to_instruction(length, payload);
  if (BUG(!received)) {
    /* this is a fatal error, as it results in different states at client
     * and middle nodes; we need to close the circuit */
    log_warn(LD_CIRC, "Cannot parse INSTRUCTION cell. Closing...");
    circuit_mark_for_close(TO_CIRCUIT(circ), END_CIRC_REASON_TORPROTOCOL);
    return -1;
  }

  if (BUG(!split_instruction_check(received, split_data->subcircs))) {
    /* the received instruction contains sub-circuit IDs that we don't know
     * about. fatal error, close the circuit */
    log_warn(LD_CIRC, "Unrecognized sub-circuit IDs. Closing...");
    circuit_mark_for_close(TO_CIRCUIT(circ), END_CIRC_REASON_TORPROTOCOL);
    return -1;
  }

  if (split_instruction_list_length(*existing_instructions) >=
      MAX_NUM_SPLIT_INSTRUCTIONS) {
    /* we have too many split instructions. close the circuit to prevent
     * a buffer exhaustion attack. */
    log_warn(LD_CIRC, "Too many split instructions. Closing...");
    circuit_mark_for_close(TO_CIRCUIT(circ), END_CIRC_REASON_TORPROTOCOL);
    return -1;
  }

  log_info(LD_CIRC, "Received %s cell on circuit %p (ID %u)",
           direction == CELL_DIRECTION_IN ? "INSTRUCTION": "INFO",
           circ, circ->p_circ_id);

  split_instruction_append(existing_instructions, received);

  if (direction == CELL_DIRECTION_OUT) {
    tor_assert(split_get_next_subcirc(split_data_get_base(split_data, 1),
        NULL, CELL_DIRECTION_OUT)); //DEBUG-split
  }

  return 0;
}
