/**
 * \file splitstrategy.h
 *
 * \brief Headers for splitstrategy.c
 **/

#ifndef TOR_SPLITSTRATEGY_H
#define TOR_SPLITSTRATEGY_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"
#include "feature/split/subcirc_list.h"

#define C_MIN   50 // Min and max values for the BWR algorithm 
#define C_MAX   70 


enum instruction_type_t {
  SPLIT_INSTRUCTION_TYPE_GENERIC = 0x00,
};

enum split_strategy_t {
  /** always choose the sub-circuit with the smallest ID */
  SPLIT_STRATEGY_MIN_ID,
  /** always choose the sub-circuit with the highest ID */
  SPLIT_STRATEGY_MAX_ID,
  /** choose the sub-circuit in a round-robin style */
  SPLIT_STRATEGY_ROUND_ROBIN,
  /** choose the sub-circuit in by a uniform random distribution */
  SPLIT_STRATEGY_RANDOM_UNIFORM,
  /** choose the sub-circuit in by a weighted biased non-uniform random distribution */
  SPLIT_STRATEGY_WEIGHTED_RANDOM,
  /** choose the sub-circuit in by a batched weighted biased non-uniform random distribution */
  SPLIT_STRATEGY_BATCHED_WEIGHTED_RANDOM,

};

#ifdef HAVE_MODULE_SPLIT
split_instruction_t* split_instruction_new(void);
void split_instruction_free_(split_instruction_t* inst);
#define split_instruction_free(inst) \
    FREE_AND_NULL(split_instruction_t, split_instruction_free_, (inst))

#else /* HAVE_MODULE_SPLIT */

static inline split_instruction_t*
split_instruction_new(void)
{
  return NULL;
}

static inline void
split_instruction_free_(split_instruction_t* inst)
{
  (void)inst; return;
}
#endif /* HAVE_MODULE_SPLIT */


/*** Internal functions (only use within the 'split' module) ***/
#ifdef MODULE_SPLIT_INTERNAL
split_instruction_t* split_payload_to_instruction(size_t length,
                                                  const uint8_t* payload);
ssize_t split_instruction_to_payload(const split_instruction_t* inst,
                                     uint8_t** payload);

split_instruction_t* split_get_new_instruction(split_strategy_t strategy,
                                               subcirc_list_t* subcircs,
                                               cell_direction_t direction,
                                               int use_prev,
					       double *prev_data);

subcirc_id_t split_instruction_get_next_id(split_instruction_t** inst_ptr);

//int split_instruction_get_left_instructions (split_instruction_t** inst_ptr);

void split_instruction_append(split_instruction_t** existing,
                              split_instruction_t* new);

int split_instruction_list_length(split_instruction_t* list);

int split_instruction_check(split_instruction_t* inst,
                            subcirc_list_t* subcircs);

void split_instruction_free_list(split_instruction_t** list);

split_strategy_t split_get_default_strategy(void);
#endif /* MODULE_SPLIT_INTERNAL */


/*** Static functions (only for testing) ***/
#ifdef TOR_SPLITSTRATEGY_PRIVATE
STATIC ssize_t parse_from_payload_generic(const uint8_t* payload,
                                          size_t payload_len,
                                          subcirc_id_t** data);
STATIC ssize_t parse_to_payload_generic(const subcirc_id_t* data,
                                        size_t data_len,
                                        uint8_t** payload);

#endif /* TOR_SPLITSTRATEGY_PRIVATE */

#endif /* TOR_SPLITSTRATEGY_H */
