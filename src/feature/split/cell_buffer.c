/**
 * \file cell_buffer.c
 *
 * \brief Implementation of the cell_buffer_t structure
 *
 * The cell_buffer_t structure is used by the split module for queue-like
 * storing of cell_t structures. It borrows heavily from cell_queue_t, the
 * main difference is, however, that cell_queue_t stores
 * <em>packed</em>_cell_t structs (instead of cell_t).
 */

#include "feature/split/cell_buffer.h"

#include "core/or/or.h"
#include "core/or/cell_st.h"

#include <string.h>

static size_t total_bytes_allocated = 0;

/** Allocate and return a new buffered_cell_t */
buffered_cell_t*
buffered_cell_new(void)
{
  buffered_cell_t* cell;
  cell = tor_malloc_zero(sizeof(buffered_cell_t));
  total_bytes_allocated += sizeof(buffered_cell_t);

  return cell;
}

/** Deallocate the storage associated with <b>cell</b>. */
void
buffered_cell_free_(buffered_cell_t* cell)
{
  if (!cell)
    return;

  tor_assert(total_bytes_allocated >= sizeof(buffered_cell_t));
  total_bytes_allocated -= sizeof(buffered_cell_t);

  tor_free(cell);
}

/** Allocate and return a new cell_buffer_t. */
cell_buffer_t*
cell_buffer_new(void)
{
  cell_buffer_t* buf;
  buf = tor_malloc_zero(sizeof(cell_buffer_t));

  return buf;
}

/** Initialise the given <b>buf</b>. */
void
cell_buffer_init(cell_buffer_t* buf)
{
  tor_assert(buf);
  TOR_SIMPLEQ_INIT(&buf->head);
}

/** Deallocate the storage associated with <b>buf</b>. */
void cell_buffer_free_(cell_buffer_t* buf)
{
  if (!buf)
    return;

  cell_buffer_clear(buf);
  tor_free(buf);
}

/** Append <b>cell</b> to the end of <b>buf</b>. */
void
cell_buffer_append(cell_buffer_t* buf, buffered_cell_t* cell)
{
  tor_assert(buf);
  tor_assert(cell);

  TOR_SIMPLEQ_INSERT_TAIL(&buf->head, cell, next);
  ++buf->num;
}

/** Create a new buffered_cell_t out of <b>cell</b> (copy data)
 * and append it to <b>buf</b>.
 */
void
cell_buffer_append_cell(cell_buffer_t* buf, const cell_t* cell)
{
  buffered_cell_t* buf_cell;
  tor_assert(buf);
  tor_assert(cell);

  buf_cell = buffered_cell_new();
  memcpy(&buf_cell->cell, cell, sizeof(cell_t));

  buf_cell->inserted_timestamp = monotime_coarse_get_stamp();

  cell_buffer_append(buf, buf_cell);
}

/** Extract and return the cell at the head of <b>buf</b>; return NULL if
 * <b>buf</b> is empty. */
buffered_cell_t*
cell_buffer_pop(cell_buffer_t* buf)
{
  buffered_cell_t* cell;
  tor_assert(buf);

  cell = TOR_SIMPLEQ_FIRST(&buf->head);
  if (!cell)
    return NULL;
  TOR_SIMPLEQ_REMOVE_HEAD(&buf->head, next);
  buf->num -= 1;
  tor_assert(buf->num >= 0);
  return cell;
}

/** Remove and free every buffered_cell_t in <b>buf</b>. Return
 * the number of bytes that were deallocated. */
size_t
cell_buffer_clear(cell_buffer_t* buf)
{
  size_t freed = 0;
  buffered_cell_t* cell;
  tor_assert(buf);

  while ((cell = TOR_SIMPLEQ_FIRST(&buf->head))) {
    TOR_SIMPLEQ_REMOVE_HEAD(&buf->head, next);
    freed += cell ? sizeof(buffered_cell_t) : 0;
    buffered_cell_free_(cell);
  }
  TOR_SIMPLEQ_INIT(&buf->head);
  buf->num = 0;

  return freed;
}

/** Return the age of the oldest cell buffered in <b>buf</b> in
 * timestamp units as measured from <b>now</b>. Return 0, if buf
 * contains no cells.
 *
 * This function will return incorrect results if the oldest buffered
 * cell is older than about 2**32 msec (about 49 days) old.
 */
uint32_t
cell_buffer_max_buffered_age(cell_buffer_t* buf, uint32_t now)
{
  uint32_t age = 0;
  buffered_cell_t* first;
  tor_assert(buf);

  /* the oldest cell is always at the beginning of the queue */
  first = TOR_SIMPLEQ_FIRST(&buf->head);
  if (first) {
    tor_assert(now >= first->inserted_timestamp);
    age = now - first->inserted_timestamp;
  }

  return age;
}

/** Return the total amount of bytes that are currently allocated to
 * store buffered cells.
 */
size_t
split_cell_buffer_get_total_allocation(void)
{
  return total_bytes_allocated;
}
