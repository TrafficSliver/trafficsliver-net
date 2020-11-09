/**
 * \file spliteval.h
 *
 * \brief Headers and necessary defines for spliteval.c
 */

#ifndef SPLIT_EVAL_H
#define SPLIT_EVAL_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"

#include <time.h>
#include <string.h>

/*** Evaluation Control ***/

/* uncomment to evaluate (split) circuit build times */
//#define SPLIT_EVAL_CIRCBUILD

/* uncomment to evaluate data rate */
//#define SPLIT_EVAL_DATARATE

#if defined(SPLIT_EVAL_CIRCBUILD) || defined(SPLIT_EVAL_DATARATE)
#define SPLIT_EVAL
#endif

#define SPLIT_EVAL_INSTRUCTIONS NUM_SPLIT_INSTRUCTIONS
#define SPLIT_EVAL_EXTEND 2

/*** Useful defines for increased readability ***/
#ifdef SPLIT_EVAL_CIRCBUILD
#define SPLIT_MEASURE(obj, timestamp_name)                                  \
  do {                                                                      \
    if (obj->split_eval_data.timestamp_name.tv_sec == 0 &&                  \
        obj->split_eval_data.timestamp_name.tv_nsec == 0)                   \
      clock_gettime(CLOCK_MONOTONIC,                                        \
          &(obj->split_eval_data.timestamp_name));                          \
  } while (0)

#define SPLIT_MMEASURE(obj, timestamp_name, max_num)                        \
  do {                                                                      \
    if (obj->split_eval_data.timestamp_name ## _count < max_num) {          \
      clock_gettime(CLOCK_MONOTONIC, &(obj->split_eval_data.timestamp_name  \
          [obj->split_eval_data.timestamp_name ## _count]));                \
      obj->split_eval_data.timestamp_name ## _count++;                      \
    }                                                                       \
  } while (0)

#define SPLIT_COPY(obj, timestamp_name, source)                             \
  do {                                                                      \
    if (obj->split_eval_data.timestamp_name.tv_sec == 0 &&                  \
        obj->split_eval_data.timestamp_name.tv_nsec == 0)                   \
      memcpy(&(obj->split_eval_data.timestamp_name), source,                \
        sizeof(struct timespec));                                           \
  } while (0)

#define SPLIT_MCOPY(obj, timestamp_name, max_num, source)                   \
  do {                                                                      \
    if (obj->split_eval_data.timestamp_name ## _count < max_num) {          \
      memcpy(&(obj->split_eval_data.timestamp_name                          \
          [obj->split_eval_data.timestamp_name ## _count]),                 \
        source, sizeof(struct timespec));                                   \
      obj->split_eval_data.timestamp_name ## _count++;                      \
    }                                                                       \
  } while (0)

#else /* SPLIT_EVAL_CIRCUBUILD */
#define SPLIT_MEASURE(obj, timestamp_name)                do {} while (0)
#define SPLIT_MMEASURE(obj, timestamp_name, max_val)      do {} while (0)
#define SPLIT_COPY(obj, timestamp_name, source)           do {} while (0)
#define SPLIT_MCOPY(obj, timestamp_name, max_num, source) do {} while (0)
#endif /* SPLIT_EVAL_CIRCUILD */

/*** Structure definitions ***/
#define TIMESTAMP(timestamp_name) struct timespec timestamp_name
#define MTIMESTAMP(timestamp_name, max_index)                               \
  struct timespec timestamp_name[max_index];                                \
  uint16_t timestamp_name ## _count

typedef struct split_eval_cell_t {
  struct split_eval_cell_t *next;
  struct split_eval_cell_t *prev;
  unsigned int num;
  struct timespec received;
  struct timespec forwarded;
} split_eval_cell_t;

typedef struct split_eval_origin_t {
  /* True, iff this circuit should be considered for performance
   * evaluation. */
  unsigned int consider:1;

  /* The sub-circuit ID of the circuit */
  subcirc_id_t id;

  /* The number of the current run */
  uint8_t run;

  TIMESTAMP(circ_allocated);
  TIMESTAMP(circ_cpath_start);
  TIMESTAMP(circ_cpath_done);
  TIMESTAMP(circ_channel_start);
  TIMESTAMP(circ_channel_done);
  TIMESTAMP(circ_build_start);
  TIMESTAMP(circ_create_tobuf);
  TIMESTAMP(circ_created_frombuf);
  MTIMESTAMP(circ_extend_tobuf, SPLIT_EVAL_EXTEND);
  MTIMESTAMP(circ_extended_frombuf, SPLIT_EVAL_EXTEND);
  TIMESTAMP(circ_build_finished);
  TIMESTAMP(split_data_created);
  TIMESTAMP(split_cookie_start);
  TIMESTAMP(split_cookie_done);
  TIMESTAMP(split_set_cookie_sent);
  TIMESTAMP(split_set_cookie_tobuf);
  TIMESTAMP(split_cookie_set_recv);
  TIMESTAMP(split_cookie_set_frombuf);
  TIMESTAMP(split_join_sent);
  TIMESTAMP(split_join_tobuf);
  TIMESTAMP(split_joined_recv);
  TIMESTAMP(split_joined_frombuf);
  MTIMESTAMP(split_instruction_sent, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_instruction_tobuf, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_info_sent, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_info_tobuf, SPLIT_EVAL_INSTRUCTIONS);
  TIMESTAMP(circ_allow_streams);
  TIMESTAMP(circ_eval_sent);
  TIMESTAMP(circ_eval_tobuf);
  TIMESTAMP(circ_begin_sent);
  TIMESTAMP(circ_begin_tobuf);
  TIMESTAMP(circ_connected_recv);
  TIMESTAMP(circ_connected_frombuf);
} split_eval_origin_t;

typedef struct split_eval_or_t {
  /* True, iff this circuit should be considered for performance
   * evaluation. */
  unsigned int consider:1;

  /* The sub-circuit ID of the circuit */
  subcirc_id_t id;

  /* The number of the current run */
  uint8_t run;

  TIMESTAMP(circ_create_frombuf);
  TIMESTAMP(circ_allocated);
  TIMESTAMP(circ_created_tobuf);
  TIMESTAMP(split_data_created);
  TIMESTAMP(split_set_cookie_recv);
  TIMESTAMP(split_set_cookie_frombuf);
  TIMESTAMP(split_cookie_set_sent);
  TIMESTAMP(split_cookie_set_tobuf);
  TIMESTAMP(split_join_recv);
  TIMESTAMP(split_join_frombuf);
  TIMESTAMP(split_joined_sent);
  TIMESTAMP(split_joined_tobuf);
  MTIMESTAMP(split_instruction_recv, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_instruction_frombuf, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_info_recv, SPLIT_EVAL_INSTRUCTIONS);
  MTIMESTAMP(split_info_frombuf, SPLIT_EVAL_INSTRUCTIONS);
  TIMESTAMP(circ_eval_recv);
  TIMESTAMP(circ_eval_frombuf);

  unsigned int num_merged_cells;
  split_eval_cell_t *merged_cells;

  unsigned int num_split_cells;
  split_eval_cell_t *split_cells;
} split_eval_or_t;

/*** Function declarations ***/
extern uint8_t split_eval_runs;

void split_eval_log_sync(void);
void split_eval_log_gettime_info(void);
int split_eval_consider(circuit_t* circ, uint8_t run);
split_eval_cell_t* split_eval_cell_new(void);

void split_eval_cell_free_(split_eval_cell_t* split_eval_cell);
#define split_eval_cell_free(split_eval_cell) \
    FREE_AND_NULL(split_eval_cell_t, split_eval_cell_free_, \
                (split_eval_cell))
void split_eval_append_cell(split_eval_or_t* eval_data,
                            cell_direction_t direction,
                            struct timespec* received,
                            struct timespec* forwarded);

void split_eval_print_circ(circuit_t* circ);
void split_eval_get_routerset(origin_circuit_t* base);

#endif /* SPLIT_EVAL_H */
