/**
 * \file subcirc_list.h
 *
 * \brief Headers for subcirclist.c
 */

#ifndef TOR_SUBCIRCLIST_H
#define TOR_SUBCIRCLIST_H

#include "feature/split/splitdefines.h"

#include <stddef.h>

#define SUBCIRC_LIST_MAX_CAPACITY MAX_SUBCIRCS
#if SUBCIRC_LIST_MAX_CAPACITY < 8
#define SUBCIRC_LIST_DEFAULT_CAPACITY SUBCIRC_LIST_MAX_CAPACITY
#else
#define SUBCIRC_LIST_DEFAULT_CAPACITY 8
#endif

typedef struct subcirc_list_t {
  void** list;
  unsigned int capacity;
  unsigned int num_elements;
  int max_index;
} subcirc_list_t;

#ifdef HAVE_MODULE_SPLIT

subcirc_list_t* subcirc_list_new(void);
void subcirc_list_free_(subcirc_list_t* sl);
#define subcirc_list_free(sl) \
    FREE_AND_NULL(subcirc_list_t, subcirc_list_free_, (sl))

void subcirc_list_add(subcirc_list_t* sl, subcircuit_t* subcirc,
                      subcirc_id_t id);
void subcirc_list_remove(subcirc_list_t* sl, subcirc_id_t id);
void subcirc_list_clear(subcirc_list_t* sl);
subcircuit_t* subcirc_list_get(subcirc_list_t* sl, subcirc_id_t id);
int subcirc_list_get_num(subcirc_list_t* sl);
int subcirc_list_contains(subcirc_list_t* sl, subcircuit_t* subcirc);

#else /* HAVE_MODULE_SPLIT */

static inline subcirc_list_t*
subcirc_list_new(void)
{
  return NULL;
}

static inline void
subcirc_list_free(subcirc_list_t* sl)
{
  (void)sl; return;
}

static inline void
subcirc_list_add(subcirc_list_t* sl, subcircuit_t* subcirc, subcirc_id_t id)
{
  (void)sl; (void)subcirc; (void)id; return;
}

static inline void
subcirc_list_remove(subcirc_list_t* sl, subcirc_id_t id)
{
  (void)sl; (void)id; return;
}

static inline void
subcirc_list_clear(subcirc_list_t* sl)
{
  (void)sl; return;
}

static inline subcircuit_t*
subcirc_list_get(subcirc_list_t* sl, subcirc_id_t id)
{
  (void)sl; (void)id; return NULL;
}

static inline int
subcirc_list_get_num(subcirc_list_t* sl)
{
  (void)sl; return 0;
}

static inline int
subcirc_list_contains(subcirc_list_t* sl, subcircuit_t* subcirc)
{
  (void)sl; (void)subcirc; return 0;
}

#endif /* HAVE_MODULE_SPLIT */

#endif /* TOR_SUBCIRCLIST_H */
