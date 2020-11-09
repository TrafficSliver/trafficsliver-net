/**
 * \file subcircuit_st.h
 *
 * \brief Definition of the struct subcircuit_t data structure
 *
 **/

#ifndef TOR_SUBCIRCUIT_H
#define TOR_SUBCIRCUIT_H

#include "core/or/or.h"
#include "feature/split/cell_buffer.h"
#include "feature/split/splitdefines.h"

enum subcirc_state_t {
  /* initial state, not added yet to split circuit */
  SUBCIRC_STATE_UNSPEC,
  /* waiting for new cookie */
  SUBCIRC_STATE_PENDING_COOKIE,
  /* waiting for join confirmation */
  SUBCIRC_STATE_PENDING_JOIN,
  /* subcirc was successfully added to a split circuit */
  SUBCIRC_STATE_ADDED,
};

struct subcircuit_t {

  /** Actual circuit associated with this struct */
  circuit_t* circ;

  /** ID of this sub-circuit (unique per split_data,
   * i.e., per merging node) */
  subcirc_id_t id;

  /** Current state of the sub-circuit */
  subcirc_state_t state;

  /** Buffer for cell reordering */
  cell_buffer_t* cell_buf;
};

#endif /*TOR_SUBCIRCUIT_H */
