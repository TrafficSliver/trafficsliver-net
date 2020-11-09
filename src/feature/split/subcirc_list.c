/**
 * \file subcirc_list.c
 *
 * \brief Resizable array list which allows storing pointers at fixed indices.
 *
 * Implementation borrows heavily from smartlists. However, these don't allow
 * us to specify a fixed index at which a pointer is stored and which does not
 * change later on.
 */

#include "feature/split/subcirc_list.h"

#include "lib/malloc/malloc.h"
#include "lib/log/util_bug.h"
#include <string.h>

/** Allocate and return the pointer to a new subcirc_list_t structure
 */
subcirc_list_t*
subcirc_list_new(void)
{
  subcirc_list_t* sl = tor_malloc(sizeof(subcirc_list_t));
  sl->max_index = -1;
  sl->num_elements = 0;
  sl->capacity = SUBCIRC_LIST_DEFAULT_CAPACITY;
  sl->list = tor_calloc(sizeof(void *), sl->capacity);
  return sl;
}

/** Deallocate the memory used by <b>sl</b> (doesn't release storage
 * associated with the pointers stored insides the list)
 */
void
subcirc_list_free_(subcirc_list_t* sl)
{
  if (!sl)
    return;
  tor_free(sl->list);
  tor_free(sl);
}

/** Ensure that <b>id</b> is a valid index of <b>sl</b>
 */
static void
subcirc_list_ensure_capacity(subcirc_list_t* sl, subcirc_id_t id)
{
  unsigned int capacity;
  int id_int = (int)id;/* type cast necessary
  to prevent compiler warning for cases that SUB_CIRC_LIST_MAX_CAPACITY
  (which equals MAX_SUBCIRCS) == (1 << sizeof(subcirc_id_t)) [in this case,
  this comparison would be always true] */

  tor_assert(sl);
  tor_assert(sl->capacity);
  tor_assert(id_int < SUBCIRC_LIST_MAX_CAPACITY);

  capacity = sl->capacity;

  if (id < capacity)
    /* no resize necessary */
    return;

  if (id >= SUBCIRC_LIST_MAX_CAPACITY / 2) {
    capacity = SUBCIRC_LIST_MAX_CAPACITY;
  } else {
    while (id >= capacity)
      capacity *= 2;
  }

  sl->list = tor_reallocarray(sl->list, sizeof(void *), (size_t)capacity);
  memset(sl->list + sl->capacity, 0,
             sizeof(void *) * (capacity - sl->capacity));

  sl->capacity = capacity;
}

/** Add a new <b>subcirc</b> to <b>sl</b> at index <b>id</b>. Ensure that
 * sl's capacity is big enough.
 */
void
subcirc_list_add(subcirc_list_t* sl, subcircuit_t* subcirc,
                 subcirc_id_t id)
{
  tor_assert(sl);
  subcirc_list_ensure_capacity(sl, id);
  tor_assert(sl->list[id] == NULL); /* no element already saved here */
  sl->list[id] = subcirc;
  sl->num_elements++;

  if (sl->max_index < (int)id)
    sl->max_index = (int)id;
}

/** Remove the element with index <b>id</b> from <b>sl</b>.
 * Do nothing, if sl contains no such element.
 * (Does not touch the stored item itself)
 */
void
subcirc_list_remove(subcirc_list_t* sl, subcirc_id_t id)
{
  tor_assert(sl);

  if (id < sl->capacity && sl->list[id]) {
    sl->list[id] = NULL;
    tor_assert((int)sl->num_elements - 1 >= 0);
    sl->num_elements = (unsigned int)(sl->num_elements - 1);

    if (sl->num_elements == 0) {
      sl->max_index = -1;
    } else if ((int)id == sl->max_index) {
      int index = sl->max_index;
      while (index >= 0) {
        if (sl->list[index]) {
          sl->max_index = index;
          break;
        }
        index--;
      }

      tor_assert(index >= 0);
    }
  }
}

/** Remove all elements from <b>sl</b>
 */
void
subcirc_list_clear(subcirc_list_t* sl)
{
  tor_assert(sl);
  memset(sl->list, 0, sizeof(void *) * sl->capacity);
  sl->max_index = -1;
  sl->num_elements = 0;
}

/** Get the subcirc whose reference is stored at index <b>id</b> in
 * <b>sl</b>. Returns NULL, if no subcirc is stored at that index.
 */
subcircuit_t*
subcirc_list_get(subcirc_list_t* sl, subcirc_id_t id)
{
  tor_assert(sl);
  tor_assert(id < sl->capacity);

  return (subcircuit_t*)sl->list[id];
}

/** Get the number of elements stored in <b>sl</b>
 */
int
subcirc_list_get_num(subcirc_list_t* sl)
{
  tor_assert(sl);

  return (int)sl->num_elements;
}

/** Return 1, if <b>sl</b> contains <b>subcirc</b>. Otherwise, return 0
 */
int
subcirc_list_contains(subcirc_list_t* sl, subcircuit_t* subcirc)
{
  tor_assert(sl);

  if (sl->num_elements == 0)
    return 0;

  tor_assert(sl->max_index >= 0);

  for (subcirc_id_t id = 0; id <= (unsigned int)sl->max_index; id++) {
    if (sl->list[id] == subcirc)
      return 1;
  }

  return 0;
}
