/**
 * \file split_data_st.h
 *
 * \brief Definition of the split_data_t, split_data_client_t,
 * split_data_or_t, and split_data_circuit_t data structures
 *
 **/

#ifndef TOR_SPLIT_DATA_ST_H
#define TOR_SPLIT_DATA_ST_H

#include "core/or/or.h"
#include "ext/ht.h"
#include "feature/split/splitdefines.h"
#include "feature/split/splitstrategy.h"
#include "feature/split/subcircuit_st.h"
#include "feature/split/subcirc_list.h"

/** An enum which specifies the state of our currently set cookie.
 */
enum split_cookie_state_t {
  /* cookie is invalid, do not use */
  SPLIT_COOKIE_STATE_INVALID,
  /* sent SET_COOKIE cell, waiting for COOKIE_SET */
  SPLIT_COOKIE_STATE_PENDING,
  /* cookie is valid */
  SPLIT_COOKIE_STATE_VALID,
};

/**
 * The split_data_client_t structure contains information for
 * operating a split circuit on the <b>client</b> side.
 */
struct split_data_client_t {

  /** list of subcirc_t* that already are in process of being added
   * to the split_data structure */
  smartlist_t* pending_subcircs;

  /** number of new subcircuits we want to launch when we get a new cookie */
  unsigned int launch_on_cookie;

  /** extend info to the merging middle node */
  extend_info_t* middle_info;

  /** remaining cpath between the middle node (excluded) and the exit
   * (included) */
  crypt_path_t* remaining_cpath;

  /** the split strategy that is currently used */
  split_strategy_t strategy;

  /** flag that is set as soon streams may be attached to the split circuit */
  unsigned int is_final:1;

  /** flags for in & out to set when the the strategy must be entery new or must use a previous ditribution data*/
  unsigned int use_previous_data_in:1;
  unsigned int use_previous_data_out:1;

  /** Data of previous distribution in case we are in the same page load*/
  double previous_data_in[MAX_SUBCIRCS];
  double previous_data_out[MAX_SUBCIRCS];

};

/**
 * The split_data_client_t structure contains information for
 * operating a split circuit on the <b>or</b>/middle side.
 */
struct split_data_or_t {

  /** reference to associated split_data structure */
  split_data_t* split_data;

  /** number of RELAY_EARLY cells we can still forward on this
   * split circuit */
  unsigned int remaining_relay_early_cells;

  HT_ENTRY(split_data_or_t) node;
};

/**
 * The split_data_t structure contains information for operating
 * a split circuit.
 */
struct split_data_t {

  /** additional information that is only needed on the client side */
  split_data_client_t* split_data_client;

  /** additional information that is only needed on the or/middle side */
  split_data_or_t* split_data_or;

  /** the base circuit of this split circuit */
  circuit_t* base;

  /** current authentication cookie */
  uint8_t cookie[SPLIT_COOKIE_LEN];

  /** state of the cookie */
  split_cookie_state_t cookie_state;

  /** list of subcircuit_t* that are part of this split circuit
   *  (sub-circuit ID matches with list index) */
  subcirc_list_t* subcircs;

  /** cache for the subcircs that should be used next on this split circuit
   * (taking cell direction into account)*/
  subcircuit_t* next_subcirc_out;
  subcircuit_t* next_subcirc_in;

  /** split instructions that are currently active */
  split_instruction_t* instruction_out;
  split_instruction_t* instruction_in;

  /** flag that indicates, whether this split_data structure has already
   * been marked for close */
  unsigned int marked_for_close:1;

};

/**
 * Data structure that resides at the base of an origin split circuit and
 * which contains information on the whole split circuit (not just on
 * one single split_data structure)
 */
struct split_data_circuit_t {

  /** number of split_data structures situated at this origin_circuit */
  int num_split_data;

  /** number of sub-circuits that have their n_chan currently blocked by
   * set_streams_blocked_on_circ */
  int num_blocked;

  /** cache for the cpaths/middles that should be used next on this split
   * circuit (taking cell direction into account) */
  crypt_path_t* next_middle_in;
  crypt_path_t* next_middle_out;

};

#endif /* TOR_SPLIT_DATA_ST_H */
