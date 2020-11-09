/**
 * \file cell_buffer.h
 *
 * \brief Headers for cell_buffer.c
 */

#ifndef TOR_CELL_BUFFER_H
#define TOR_CELL_BUFFER_H

#include "core/or/or.h"
#include "core/or/cell_st.h"
#include "ext/tor_queue.h"

/** Wrapper for a buffered cell */
typedef struct buffered_cell_t {
  /** Next buffered cell in the queue */
  TOR_SIMPLEQ_ENTRY(buffered_cell_t) next;

  /** Actual cell */
  cell_t cell;

  /** Time (in timestamp units) when this cell was inserted */
  uint32_t inserted_timestamp;
} buffered_cell_t;

/** Cell buffer queue */
typedef struct cell_buffer_t {
  /** Linked list of buffered_cell_t*/
  TOR_SIMPLEQ_HEAD(buffered_cell_q, buffered_cell_t) head;

  /** The number of cells in the queue. */
  int num;
} cell_buffer_t;

/*** Mutation functions ***/

#ifdef HAVE_MODULE_SPLIT

buffered_cell_t* buffered_cell_new(void);
void buffered_cell_free_(buffered_cell_t* cell);
#define buffered_cell_free(cell) \
  FREE_AND_NULL(buffered_cell_t, buffered_cell_free_, cell)

cell_buffer_t* cell_buffer_new(void);
void cell_buffer_init(cell_buffer_t* buf);
void cell_buffer_free_(cell_buffer_t* buf);
#define cell_buffer_free(buf) \
  FREE_AND_NULL(cell_buffer_t, cell_buffer_free_, buf)

void cell_buffer_append(cell_buffer_t* buf, buffered_cell_t* cell);
void cell_buffer_append_cell(cell_buffer_t* buf, const cell_t* cell);
buffered_cell_t* cell_buffer_pop(cell_buffer_t* buf);
size_t cell_buffer_clear(cell_buffer_t* buf);
uint32_t cell_buffer_max_buffered_age(cell_buffer_t* buf, uint32_t now);

size_t split_cell_buffer_get_total_allocation(void);

#else /* HAVE_MODULE_SPLIT */

static inline buffered_cell_t*
buffered_cell_new(void)
{
  return NULL;
}

static inline void
buffered_cell_free(buffered_cell_t* cell)
{
  (void)cell; return;
}

static inline cell_buffer_t*
cell_buffer_new(void)
{
  return NULL;
}

static inline void
cell_buffer_init(cell_buffer_t* buf)
{
  (void)buf; return;
}

static inline void
cell_buffer_free_(cell_buffer_t* buf)
{
  (void)buf; return;
}

static inline void
cell_buffer_append(cell_buffer_t* buf, buffered_cell_t* cell)
{
  (void)buf; (void)cell; return;
}

static inline void
cell_buffer_append_cell(cell_buffer_t* buf, const cell_t* cell)
{
  (void)buf; (void)cell; return;
}

static inline buffered_cell_t*
cell_buffer_pop(cell_buffer_t* buf)
{
  (void)buf; return NULL;
}

static inline size_t
cell_buffer_clear(cell_buffer_t* buf)
{
  (void)buf; return 0;
}

static inline uint32_t
cell_buffer_max_buffered_age(cell_buffer_t* buf, uint32_t now)
{
  (void)buf; (void)now; return 0;
}

static inline size_t
split_cell_buffer_get_total_allocation(void)
{
  return 0;
}

#endif /* HAVE_MODULE_SPLIT */

#endif /* TOR_CELL_BUFFER_H */
