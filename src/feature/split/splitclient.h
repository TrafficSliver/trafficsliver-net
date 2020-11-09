/**
 * \file splitclient.h
 *
 * \brief Headers for splitclient.c
 **/

#ifndef TOR_SPLITCLIENT_H
#define TOR_SPLITCLIENT_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"

#ifdef HAVE_MODULE_SPLIT

int split_launch_subcircuit(origin_circuit_t* circ, crypt_path_t* middle,
                            int num);

smartlist_t* split_data_get_excluded_nodes(split_data_t* split_data);

void split_join_has_opened(origin_circuit_t* circ);

int split_may_attach_stream(const origin_circuit_t* circ, int must_be_open);

void split_data_finalise(split_data_t* split_data);

void split_next_if_name(origin_circuit_t* base, char* if_name, size_t len);

unsigned int split_get_subcircs_per_circ(void);

#else /* HAVE_MODULE_SPLIT */

static inline int
split_launch_subcircuit(origin_circuit_t* circ, crypt_path_t* middle, int num)
{
  (void)circ; (void)middle; (void)num; return 0;
}

static inline smartlist_t*
split_data_get_excluded_nodes(split_data_t* split_data)
{
  (void)split_data; return NULL;
}

static inline void
split_join_has_opened(origin_circuit_t* circ)
{
  (void)circ; return;
}

static inline int
split_may_attach_stream(const origin_circuit_t* circ, int must_be_open)
{
  (void)circ; (void)must_be_open; return 1;
}

static inline void
split_data_finalise(split_data_t* split_data)
{
  (void)split_data; return;
}

static inline void
split_next_if_name(origin_circuit_t* base, char* if_name, size_t len)
{
  (void)base; strlcpy(if_name, "", len); return;
}

static inline unsigned int
split_get_subcircs_per_circ(void)
{
  return 0;
}

#endif /* HAVE_MODULE_SPLIT */

/*** Internal functions (only use within the 'split' module) ***/
#ifdef MODULE_SPLIT_INTERNAL

int split_process_cookie_set(origin_circuit_t* circ, crypt_path_t* middle,
                             size_t length, const uint8_t* payload);

int split_process_joined(origin_circuit_t* circ, crypt_path_t* middle,
                         size_t length, const uint8_t* payload);

int split_data_generate_instruction(split_data_t* split_data,
                                    cell_direction_t direction);

#endif /* MODULE_SPLIT_INTERNAL */

#endif /* TOR_SPLITCLIENT_H */
