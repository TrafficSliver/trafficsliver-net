/**
 * \file spliteval.c
 *
 * \brief Implementation of performance evaluation functions
 */

#include "feature/split/spliteval.h"

#include "core/or/circuitlist.h"
#include "core/or/or.h"
#include "core/or/relay.h"
#include "feature/split/splitcommon.h"
#include "feature/split/splitdefines.h"
#include "feature/split/splitutil.h"

#include "core/or/circuit_st.h"
#include "core/or/extend_info_st.h"
#include "core/or/or_circuit_st.h"
#include "core/or/origin_circuit_st.h"
#include "lib/crypt_ops/crypto_digest.h"
#include "feature/nodelist/routerset.h"
#include "lib/log/log.h"
#include "app/config/config.h"

#include <time.h>

#define TIME_FORMAT "%lus%09luns"
#define TIME_EXTRACT(timespec) (timespec).tv_sec, (timespec).tv_nsec

/* Keep track of the number of runs */
uint8_t split_eval_runs = 0;

void
split_eval_log_sync(void)
{
  struct timespec realtime, monotonic;

  clock_gettime(CLOCK_MONOTONIC, &monotonic);
  clock_gettime(CLOCK_REALTIME, &realtime);

  log_notice(LD_GENERAL, "SYNC:"TIME_FORMAT":"TIME_FORMAT,
             TIME_EXTRACT(monotonic), TIME_EXTRACT(realtime));
}

void
split_eval_log_gettime_info(void)
{
  struct timespec res, time1, time2;

  clock_getres(CLOCK_MONOTONIC, &res);
  clock_gettime(CLOCK_MONOTONIC, &time1);
  clock_gettime(CLOCK_MONOTONIC, &time2);

  log_notice(LD_GENERAL, "CLOCKRES:"TIME_FORMAT, TIME_EXTRACT(res));

  log_notice(LD_GENERAL, "GETTIME_DURATION:"TIME_FORMAT":"TIME_FORMAT,
             TIME_EXTRACT(time1), TIME_EXTRACT(time2));
}

#if defined(SPLIT_EVAL)
static void
split_eval_consider_split_data(split_data_t* split_data, uint8_t run)
{
  tor_assert(split_data);
  subcircuit_t* subcirc;

  for (subcirc_id_t id = 0; (int)id <= split_data->subcircs->max_index; id++) {
    subcirc = subcirc_list_get(split_data->subcircs, id);
    if (subcirc) {
      tor_assert(subcirc->circ);
      if (CIRCUIT_IS_ORIGIN(subcirc->circ)) {
        TO_ORIGIN_CIRCUIT(subcirc->circ)->split_eval_data.consider = 1;
        TO_ORIGIN_CIRCUIT(subcirc->circ)->split_eval_data.id = subcirc->id;
        TO_ORIGIN_CIRCUIT(subcirc->circ)->split_eval_data.run = run;
      }
      else {
        TO_OR_CIRCUIT(subcirc->circ)->split_eval_data.consider = 1;
        TO_OR_CIRCUIT(subcirc->circ)->split_eval_data.id = subcirc->id;
        TO_OR_CIRCUIT(subcirc->circ)->split_eval_data.run = run;
      }
    }
  }
}

/* Mark this <b>circ</b> (and all its sub-circuits) as to be considered
 * for our performance evaluation. Also, tell the middle node that it should
 * do the same for this circuit (and all its sub-circuits).
 *
 * Return 0 on success and -1 on failure.
 */
int
split_eval_consider(circuit_t* circ, uint8_t run)
{
  tor_assert(circ);

  if (CIRCUIT_IS_ORIGIN(circ)) {
    origin_circuit_t* origin_circ = TO_ORIGIN_CIRCUIT(circ);
    crypt_path_t* middle;
    char payload;

    if (origin_circ->split_eval_data.consider) {
      log_info(LD_CIRC, "We are already considering circuit %p (and "
               "its possible sub-circuits). Done...", origin_circ);
      return 0;
    }

    tor_assert(origin_circ->cpath);
    middle = origin_circ->cpath->next;

    log_info(LD_CIRC, "Sending a SPLIT_EVAL cell on circ %p (ID %u) to "
             "middle %s", origin_circ, circ->n_circ_id,
             middle->extend_info->nickname);

    payload = (char)run;
    if (relay_send_command_from_edge(0, circ,
                                     RELAY_COMMAND_SPLIT_EVAL, &payload, 1,
                                     middle)) {
      log_warn(LD_CIRC, "Could not send SPLIT_EVAL cell to the middle node. "
               "Closing...");
      return -1;
    }

    if (middle->split_data)
      split_eval_consider_split_data(middle->split_data, run);
    else {
      origin_circ->split_eval_data.consider = 1;
      origin_circ->split_eval_data.run = run;
    }

  } else { /* it's an or_circuit */
    or_circuit_t* or_circ = TO_OR_CIRCUIT(circ);

    if (or_circ->split_data)
      split_eval_consider_split_data(or_circ->split_data, run);
    else {
      or_circ->split_eval_data.consider = 1;
      or_circ->split_eval_data.run = run;
    }
  }

  return 0;
}
#else /* SPLIT_EVAL not defined */
int
split_eval_consider(circuit_t* circ, uint8_t run)
{
  (void)circ; (void)run; return 0;
}
#endif

split_eval_cell_t*
split_eval_cell_new(void)
{
  split_eval_cell_t* split_eval_cell;
  split_eval_cell = tor_malloc_zero(sizeof(split_eval_cell_t));

  return split_eval_cell;
}

void
split_eval_cell_free_(split_eval_cell_t* split_eval_cell)
{
  if (!split_eval_cell)
    return;

  split_eval_cell_free(split_eval_cell->next);

  tor_free(split_eval_cell);
}

void
split_eval_append_cell(split_eval_or_t* eval_data, cell_direction_t direction,
                       struct timespec* received, struct timespec* forwarded)
{
  split_eval_cell_t** cells;
  split_eval_cell_t* next;
  tor_assert(eval_data);
  tor_assert(received);
  tor_assert(forwarded);

  next = split_eval_cell_new();

  if (direction == CELL_DIRECTION_OUT) {
    cells = &eval_data->merged_cells;
    next->num = eval_data->num_merged_cells++;
  } else { /* CELL_DIRECTION_IN */
    cells = &eval_data->split_cells;
    next->num = eval_data->num_split_cells++;
  }

  if (*cells) {
    (*cells)->prev->next = next;
    (*cells)->prev = next;
  } else {
    (*cells) = next;
    (*cells)->prev = next;
  }

  memcpy(&next->received, received, sizeof(struct timespec));
  memcpy(&next->forwarded, forwarded, sizeof(struct timespec));
}

static void
split_eval_print_timestamp(const char* identifier, const char* label,
                           int index, struct timespec* timestamp)
{
  char index_string[12] = "";

  if (index >= 0) {
    tor_snprintf(index_string, 12, "_%d", index);
  }

  if (timestamp->tv_sec != 0 || timestamp->tv_nsec != 0) {
    log_notice(LD_GENERAL, "%s:%s%s:"TIME_FORMAT,
               identifier ? identifier : "",
               label, index >= 0 ? index_string : "",
               TIME_EXTRACT(*timestamp));
  }
}

#if defined(SPLIT_EVAL)
#define print(label, timestamp_name)                                        \
  split_eval_print_timestamp(identifier, label, -1,                         \
      &o_circ->split_eval_data.timestamp_name)

#define mprint(label, timestamp_name)                                       \
  unsigned int timestamp_name ## _iter = 0;                                 \
  while(timestamp_name ##_iter < o_circ->split_eval_data.timestamp_name     \
      ## _count) {                                                          \
    split_eval_print_timestamp(identifier, label, timestamp_name##_iter + 1,\
      &o_circ->split_eval_data.timestamp_name[timestamp_name ## _iter]);    \
    timestamp_name ## _iter++;                                              \
  }

void
split_eval_print_circ(circuit_t* circ)
{
#define indentifier_ft  "RUN%u:CIRC%u"
#define headline_ft     "**** CIRCUIT %p (ID %u) closed (purpose %s) ****"
  char identifier[20] = "";
  tor_assert(circ);

  if (CIRCUIT_IS_ORIGIN(circ)) {
    origin_circuit_t* o_circ = TO_ORIGIN_CIRCUIT(circ);

    if (!o_circ->split_eval_data.consider)
      return;

    tor_snprintf(identifier, 20, indentifier_ft, o_circ->split_eval_data.run,
                 o_circ->split_eval_data.id);

    log_notice(LD_GENERAL, headline_ft, o_circ, circ->n_circ_id,
               circuit_purpose_to_controller_string(circ->purpose));

    print("CIRC_ALLOC", circ_allocated);
    print("CPATH_START", circ_cpath_start);
    print("CPATH_DONE", circ_cpath_done);
    print("CHAN_START", circ_channel_start);
    print("CHAN_DONE", circ_channel_done);
    print("BUILD_START", circ_build_start);
    print("CREATE_TOBUF", circ_create_tobuf);
    print("CREATED_FROMBUF", circ_created_frombuf);
    mprint("EXTEND_TOBUF", circ_extend_tobuf);
    mprint("EXTENDED_FROMBUF", circ_extended_frombuf);
    print("BUILD_FINISHED", circ_build_finished);
    print("SPLIT_DATA", split_data_created);
    print("COOKIE_START", split_cookie_start);
    print("COOKIE_DONE", split_cookie_done);
    print("SET_COOKIE_SENT", split_set_cookie_sent);
    print("SET_COOKIE_TOBUF", split_set_cookie_tobuf);
    print("COOKIE_SET_FROMBUF", split_cookie_set_frombuf);
    print("COOKIE_SET_RECV", split_cookie_set_recv);
    print("JOIN_SENT", split_join_sent);
    print("JOIN_TOBUF", split_join_tobuf);
    print("JOINED_FROMBUF", split_joined_frombuf);
    print("JOINED_RECV", split_joined_recv);
    mprint("INSTRUCTION_SENT", split_instruction_sent);
    mprint("INSTRUCTION_TOBUF", split_instruction_tobuf);
    mprint("INFO_SENT", split_info_sent);
    mprint("INFO_TOBUF", split_info_tobuf);
    print("ALLOW_STREAMS", circ_allow_streams);
    print("EVAL_SENT", circ_eval_sent);
    print("EVAL_TOBUF", circ_eval_tobuf);
    print("BEGIN_SENT", circ_begin_sent);
    print("BEGIN_TOBUF", circ_begin_tobuf);
    print("CONNECTED_FROMBUF", circ_connected_frombuf);
    print("CONNECTED_RECV", circ_connected_recv);

  } else {
    or_circuit_t* o_circ = TO_OR_CIRCUIT(circ);

    if (!o_circ->split_eval_data.consider)
      return;

    tor_snprintf(identifier, 20, indentifier_ft, o_circ->split_eval_data.run,
                 o_circ->split_eval_data.id);

    log_notice(LD_GENERAL, headline_ft, o_circ, o_circ->p_circ_id,
               circuit_purpose_to_controller_string(circ->purpose));

    print("CREATE_FROMBUF", circ_create_frombuf);
    print("CIRC_ALLOC", circ_allocated);
    print("CREATED_TOBUF", circ_created_tobuf);
    print("SPLIT_DATA", split_data_created);
    print("SET_COOKIE_FROMBUF", split_set_cookie_frombuf);
    print("SET_COOKIE_RECV", split_set_cookie_recv);
    print("COOKIE_SET_SENT", split_cookie_set_sent);
    print("COOKIE_SET_TOBUF", split_cookie_set_tobuf);
    print("JOIN_FROMBUF", split_join_frombuf);
    print("JOIN_RECV", split_join_recv);
    print("JOINED_SENT", split_joined_sent);
    print("JOINED_TOBUF", split_joined_tobuf);
    mprint("INSTRUCTION_FROMBUF", split_instruction_frombuf);
    mprint("INSTRUCTION_RECV", split_instruction_recv);
    mprint("INFO_FROMBUF", split_info_frombuf);
    mprint("INFO_RECV", split_info_recv);
    print("EVAL_FROMBUF", circ_eval_frombuf);
    print("EVAL_RECV", circ_eval_recv);

    /* print cell data */
    split_eval_cell_t* merged_cell = o_circ->split_eval_data.merged_cells;
    while (merged_cell) {
      split_eval_print_timestamp(identifier, "MERGED_CELL_FROMBUF",
                                 (int)merged_cell->num,
                                 &merged_cell->received);
      split_eval_print_timestamp(identifier, "MERGED_CELL_TOBUF",
                                 (int)merged_cell->num,
                                 &merged_cell->forwarded);
      merged_cell = merged_cell->next;
    }
    split_eval_cell_free(o_circ->split_eval_data.merged_cells);

    split_eval_cell_t* split_cell = o_circ->split_eval_data.split_cells;
    while (split_cell) {
      split_eval_print_timestamp(identifier, "SPLIT_CELL_FROMBUF",
                                 (int)split_cell->num,
                                 &split_cell->received);
      split_eval_print_timestamp(identifier, "SPLIT_CELL_TOBUF",
                                 (int)split_cell->num,
                                 &split_cell->forwarded);
      split_cell = split_cell->next;
    }
    split_eval_cell_free(o_circ->split_eval_data.split_cells);
  }

}
#else /* SPLIT_EVAL not defined */
void
split_eval_print_circ(circuit_t* circ)
{
  (void)&split_eval_print_timestamp; (void)circ; return;
}
#endif


#if defined(SPLIT_EVAL)

static char*
split_eval_cpath_to_hexdigest(crypt_path_t* source)
{
  char* destination;

  tor_assert(source);
  tor_assert(source->extend_info);

  destination = tor_malloc(HEX_DIGEST_LEN + 1);
  base16_encode(destination, HEX_DIGEST_LEN+1, source->extend_info->identity_digest, DIGEST_LEN);

  return destination;
}

/**
 * Write the fingerprints of the nodes used for <b>base</b> and its sub-circuits to
 * the log (for later use with torrc Split[Entry/Middle/Exit]Nodes. Also, update the
 * currently loaded configuration options if necessary.
 * Assumes that <b>base</b> is the base-circuit of a potential split circuit.
 */
void
split_eval_get_routerset(origin_circuit_t* base)
{
  char* entry_hexdigest[MAX_SUBCIRCS] = {0};
  char* middle_hexdigest = NULL;
  char* exit_hexdigest = NULL;
  split_data_t* split_data;
  subcircuit_t* subcirc;
  or_options_t* options = get_options_mutable();

  tor_assert(base);

  /* extract hexdigests from base */
  entry_hexdigest[0] = split_eval_cpath_to_hexdigest(base->cpath);
  middle_hexdigest = split_eval_cpath_to_hexdigest(base->cpath->next);
  exit_hexdigest = split_eval_cpath_to_hexdigest(base->cpath->prev);

  /* extract entry-hexdigests from potential sub-circuits */
  split_data = base->cpath->next->split_data;
  if (split_data) {
    tor_assert(subcirc_list_get(split_data->subcircs, 0) == base->cpath->next->subcirc); //DEBUG-split
    for (subcirc_id_t id = 1; (int)id <= split_data->subcircs->max_index; id++) {
      subcirc = subcirc_list_get(split_data->subcircs, id);
      if (subcirc) {
        tor_assert(CIRCUIT_IS_ORIGIN(subcirc->circ));
        entry_hexdigest[id] = split_eval_cpath_to_hexdigest(TO_ORIGIN_CIRCUIT(subcirc->circ)->cpath);
      }
    }
  }

  /* write to log and update current configuration (if necessary) */
  if (!options->SplitEntryNodes) {
    options->SplitEntryNodes = routerset_new();
    for (int i = 0; i < MAX_SUBCIRCS; i++) {
      if (entry_hexdigest[i]) {
        routerset_parse(options->SplitEntryNodes, entry_hexdigest[i], "SplitEntryNodes");
        log_notice(LD_GENERAL, "SplitEntryNodes%d:%s", i, entry_hexdigest[i]);
      }
    }
  }

  if (!options->SplitMiddleNodes) {
    options->SplitMiddleNodes = routerset_new();
    routerset_parse(options->SplitMiddleNodes, middle_hexdigest, "SplitMiddleNodes");
    log_notice(LD_GENERAL, "SplitMiddleNodes:%s", middle_hexdigest);
  }

  if (!options->SplitExitNodes) {
    options->SplitExitNodes = routerset_new();
    routerset_parse(options->SplitExitNodes, exit_hexdigest, "SplitExitNodes");
    log_notice(LD_GENERAL, "SplitExitNodes:%s", exit_hexdigest);
  }


  for (int i = 0; i < MAX_SUBCIRCS; i++) {
    tor_free(entry_hexdigest[i]);
  }
  tor_free(middle_hexdigest);
  tor_free(exit_hexdigest);
}
#else /* SPLIT_EVAL not defined */
void
split_eval_get_routerset(origin_circuit_t* base)
{
  (void)&base;
}
#endif
