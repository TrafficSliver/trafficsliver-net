/**
 * \file splitcommon.h
 *
 * \brief Headers for splitcommon.c
 **/

#ifndef TOR_SPLITCOMMON_H
#define TOR_SPLITCOMMON_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"
#include "feature/split/split_data_st.h"

#ifdef HAVE_MODULE_SPLIT

split_data_t* split_data_new(void);
void split_data_init_client(split_data_t* split_data, origin_circuit_t* base,
                            crypt_path_t* middle);
void split_data_init_or(split_data_t* split_data, or_circuit_t* base);
void split_data_free_(split_data_t* split_data);
#define split_data_free(split_data) \
    FREE_AND_NULL(split_data_t, split_data_free_, (split_data))

circuit_t* split_data_get_base(split_data_t* split_data, int must_be_added);

subcircuit_t* split_data_get_next_subcirc(split_data_t* split_data,
                                          cell_direction_t direction);
void split_data_used_subcirc(split_data_t* split_data,
                             cell_direction_t direction);

void split_process_relay_cell(circuit_t* circ, crypt_path_t* layer_hint,
                              cell_t* cell, int command, size_t length,
                              const uint8_t* payload);

void split_mark_for_close(circuit_t *circ, int reason);

void split_remove_subcirc(circuit_t* circ, int at_exit);

circuit_t* split_is_relevant(circuit_t *circ, crypt_path_t* layer_hint);

crypt_path_t* split_find_equal_cpath(circuit_t* new_circ,
                                     crypt_path_t* old_cpath_layer);

circuit_t* split_get_base_(circuit_t* circ);

circuit_t* split_get_base(circuit_t* circ);

subcircuit_t* split_get_next_subcirc(circuit_t* circ,
                                     crypt_path_t* layer_hint,
                                     cell_direction_t direction);

split_data_t* split_get_next_split_data(circuit_t* circ,
                                        crypt_path_t* layer_hint,
                                        cell_direction_t direction);

void split_used_circuit(circuit_t* circ, cell_direction_t direction);

void split_base_inc_blocked(circuit_t* base);
void split_base_dec_blocked(circuit_t* base);
int split_base_should_unblock(circuit_t* base);

void split_buffer_cell(subcircuit_t* subcirc, cell_t* cell);

void split_handle_buffered_cells(circuit_t* circ);

uint32_t split_max_buffered_cell_age(const circuit_t* circ, uint32_t now);
size_t split_marked_circuit_free_buffer(circuit_t* circ);

#else /* HAVE_MODULE_SPLIT */

static inline split_data_t*
split_data_new(void)
{
  return NULL;
}

static inline void
split_data_init_client(split_data_t* split_data, origin_circuit_t* base,
                       crypt_path_t* middle)
{
  (void)split_data; (void)base; (void)middle; return;
}

static inline void
split_data_init_or(split_data_t* split_data, or_circuit_t* base)
{
  (void)split_data; (void)base; return;
}

static inline void
split_data_free(split_data_t* split_data)
{
  (void)split_data; return;
}

static inline circuit_t*
split_data_get_base(split_data_t* split_data, int must_be_added)
{
  (void)split_data; (void)must_be_added; return NULL;
}

static inline subcircuit_t*
split_data_get_next_subcirc(split_data_t* split_data,
                            cell_direction_t direction)
{
  (void)split_data; (void)direction; return NULL;
}

static inline void
split_data_used_subcirc(split_data_t* split_data, cell_direction_t direction)
{
  (void)split_data; (void)direction;
}

static inline void
split_process_relay_cell(circuit_t* circ, crypt_path_t* layer_hint,
                         cell_t* cell, int command, size_t length,
                         const uint8_t* payload)
{
  (void)(circ); (void)(layer_hint); (void)cell; (void)(command);
  (void)(length); (void)(payload);
  log_notice(LD_PROTOCOL, "Traffic splitting module is deactivated in this"
             "build. Dropping cell with relay command %d.", command);
}

static inline void
split_mark_for_close(circuit_t *circ, int reason)
{
  (void)circ; (void)reason; return;
}

static inline void
split_remove_subcirc(circuit_t* circ, int at_exit)
{
  (void)circ; (void)at_exit; return;
}

static inline circuit_t*
split_is_relevant(circuit_t *circ, crypt_path_t* layer_hint)
{
  (void)circ; (void)layer_hint; return NULL;
}

static inline crypt_path_t*
split_find_equal_cpath(circuit_t* new_circ,
                       crypt_path_t* old_cpath_layer)
{
  (void)new_circ; return old_cpath_layer;
}

static inline circuit_t*
split_get_base_(circuit_t* circ)
{
  (void)circ; return NULL;
}

static inline circuit_t*
split_get_base(circuit_t* circ)
{
  return circ;
}

static inline subcircuit_t*
split_get_next_subcirc(circuit_t* base, crypt_path_t* dest,
                       cell_direction_t direction)
{
  (void)base; (void)dest; (void)direction; return NULL;
}

static inline split_data_t*
split_get_next_split_data(circuit_t* base, crypt_path_t* dest,
                          cell_direction_t direction)
{
  (void)base; (void)dest; (void)direction; return NULL;
}

static inline void
split_used_circuit(circuit_t* base, cell_direction_t direction)
{
  (void)base; (void)direction; return;
}

static inline void
split_base_inc_blocked(circuit_t* base)
{
  (void)base; return;
}

static inline void
split_base_dec_blocked(circuit_t* base)
{
  (void)base; return;
}

static inline int
split_base_should_unblock(circuit_t* base)
{
  (void)base; return 1;
}

static inline void
split_buffer_cell(subcircuit_t* subcirc, cell_t* cell)
{
  (void)subcirc; (void)cell; return;
}

static inline void
split_handle_buffered_cells(circuit_t* circ)
{
  (void)circ; return;
}

static inline uint32_t
split_max_buffered_cell_age(const circuit_t* circ, uint32_t now)
{
  (void)circ; (void)now; return 0;
}

static inline size_t
split_marked_circuit_free_buffer(circuit_t* circ)
{
  (void)circ; return 0;
}

#endif /* HAVE_MODULE_SPLIT */

/*** Internal functions (only use within the 'split' module) ***/

#ifdef MODULE_SPLIT_INTERNAL

split_data_client_t* split_data_client_new(void);
void split_data_client_init(split_data_client_t* split_data_client,
                            origin_circuit_t* base, crypt_path_t* middle);
void split_data_client_free_(split_data_client_t* split_data_client);
#define split_data_client_free(split_data_client) \
   FREE_AND_NULL(split_data_client_t, split_data_client_free_, \
                (split_data_client))

split_data_or_t* split_data_or_new(void);
void split_data_or_init(split_data_or_t* split_data_or,
                        split_data_t* split_data, or_circuit_t* base);
void split_data_or_free_(split_data_or_t* split_data_or);
#define split_data_or_free(split_data_or) \
   FREE_AND_NULL(split_data_or_t, split_data_or_free_, \
                (split_data_or))

split_data_circuit_t* split_data_circuit_new(void);
void split_data_circuit_free_(split_data_circuit_t* split_data_circuit);
#define split_data_circuit_free(split_data_circuit) \
    FREE_AND_NULL(split_data_circuit_t, split_data_circuit_free_, \
                 (split_data_circuit))

subcircuit_t* subcircuit_new(void);
void subcircuit_free_(subcircuit_t* subcirc);
#define subcircuit_free(subcirc) \
    FREE_AND_NULL(subcircuit_t, subcircuit_free_, (subcirc))

subcircuit_t* split_data_get_subcirc(split_data_t* split_data,
                                     subcirc_id_t id);
unsigned int split_data_get_num_subcircs(split_data_t* split_data);
unsigned int split_data_get_num_subcircs_pending(split_data_t* split_data);
unsigned int split_data_get_num_subcircs_added(split_data_t* split_data);
subcircuit_t* split_data_add_subcirc(split_data_t* split_data,
                  subcirc_state_t state, circuit_t* circ, subcirc_id_t id);
int split_data_check_subcirc(split_data_t* split_data, circuit_t* circ);
void split_data_remove_subcirc(split_data_t** split_data_ptr,
                  subcircuit_t** subcirc_ptr, int at_exit);
void split_data_reset_next_subcirc(split_data_t* split_data);

const char* subcirc_state_str(subcirc_state_t state);
void subcirc_change_state(subcircuit_t* subcirc, subcirc_state_t new_state);

#endif /* MODULE_SPLIT_INTERNAL */

#endif /* TOR_SPLITCOMMON_H */
