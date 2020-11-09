/**
 * \file splitor.h
 *
 * \brief Headers for splitor.c
 **/

#ifndef TOR_SPLITOR_H
#define TOR_SPLITOR_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"

#ifdef HAVE_MODULE_SPLIT

void split_decrease_remaining_relay_early(or_circuit_t* circ);

void split_rewrite_relay_early(or_circuit_t* circ, cell_t* cell);

#else /* HAVE_MODULE_SPLIT */

static inline void
split_decrease_remaining_relay_early(or_circuit_t* circ)
{
  (void)circ; return;
}

static inline void
split_rewrite_relay_early(or_circuit_t* circ, cell_t* cell)
{
  (void)circ; (void)cell; return;
}

#endif /* HAVE_MODULE_SPLIT */

/*** Internal functions (only use within the 'split' module) ***/
#ifdef MODULE_SPLIT_INTERNAL

int split_process_set_cookie(or_circuit_t* circ, size_t length,
                             const uint8_t* payload);

int split_process_join(or_circuit_t* circ, size_t length,
                       const uint8_t* payload);

void split_data_cookie_make_invalid(split_data_t* split_data);

int split_process_instruction(or_circuit_t* circ, size_t length,
                              const uint8_t* payload,
                              cell_direction_t direction);

#endif /* MODULE_SPLIT_INTERNAL */

#endif /* TOR_SPLITOR_H */
