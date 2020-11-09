#include "core/or/or.h"
#include "test/test.h"

#include "feature/split/splitdefines.h"
#include "feature/split/subcirc_list.h"
#include "feature/split/subcircuit_st.h"

static void
test_subcirc_list_new(void* arg)
{
  subcirc_list_t* list = NULL;
  (void)arg;

  list = subcirc_list_new();

  tt_assert(list);
  tt_assert(list->capacity == SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_assert(list->max_index == -1);
  tt_assert(list->num_elements == 0);
  tt_assert(list->list);

  for (unsigned int i = 0; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  done:
  subcirc_list_free(list);
}

static void
test_subcirc_list_free(void* arg)
{
  subcirc_list_t* list = NULL;
  (void)arg;

  /* no-op on NULL pointer */
  subcirc_list_free(list);
  tt_ptr_op(list, OP_EQ, NULL);

  list = subcirc_list_new();
  subcirc_list_free(list);
  tt_ptr_op(list, OP_EQ, NULL);

  done:
  return;
}

static void
test_subcirc_list_add_noresize(void* arg)
{
  subcirc_list_t* list = NULL;
  subcircuit_t dummy1;
  subcircuit_t dummy2;
  subcircuit_t dummy3;
  subcirc_id_t id1 = 2;
  subcirc_id_t id2 = 0;
  subcirc_id_t id3 = (subcirc_id_t)(SUBCIRC_LIST_DEFAULT_CAPACITY - 1 > 0 ?
      SUBCIRC_LIST_DEFAULT_CAPACITY - 1 : 0);
  unsigned int i;
  (void)arg;

  list = subcirc_list_new();

  subcirc_list_add(list, &dummy1, id1);
  tt_ptr_op(list->list[id1], OP_EQ, &dummy1);
  tt_ptr_op(subcirc_list_get(list, id1), OP_EQ, &dummy1);
  tt_int_op(list->num_elements, OP_EQ, 1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 1);
  tt_int_op(list->capacity, OP_EQ, SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id1);

  for (i = 0; i < id1; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  for (i = id1 + 1; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  subcirc_list_add(list, &dummy2, id2);
  tt_ptr_op(list->list[id2], OP_EQ, &dummy2);
  tt_ptr_op(subcirc_list_get(list, id2), OP_EQ, &dummy2);
  tt_int_op(list->num_elements, OP_EQ, 2);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 2);
  tt_int_op(list->capacity, OP_EQ, SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id1);

  subcirc_list_add(list, &dummy3, id3);
  tt_ptr_op(list->list[id3], OP_EQ, &dummy3);
  tt_ptr_op(subcirc_list_get(list, id3), OP_EQ, &dummy3);
  tt_int_op(list->num_elements, OP_EQ, 3);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 3);
  tt_int_op(list->capacity, OP_EQ, SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id3);

  done:
  subcirc_list_free(list);
}

static void
test_subcirc_list_add_resize(void* arg)
{
  subcirc_list_t* list = NULL;
  subcircuit_t dummy1;
  subcircuit_t dummy2;
  subcircuit_t dummy3;
  subcirc_id_t id1 = 0;
  subcirc_id_t id2 = SUBCIRC_LIST_DEFAULT_CAPACITY;
  subcirc_id_t id3 = 8 * SUBCIRC_LIST_DEFAULT_CAPACITY + 1;
  unsigned int i;
  (void)arg;

  list = subcirc_list_new();

  subcirc_list_add(list, &dummy1, id1);
  tt_ptr_op(subcirc_list_get(list, id1), OP_EQ, &dummy1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 1);
  tt_int_op(list->capacity, OP_EQ, SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id1);

  for (i = id1 + 1; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  if (id2 >= SUBCIRC_LIST_MAX_CAPACITY)
    goto done;
  subcirc_list_add(list, &dummy2, id2);
  tt_ptr_op(subcirc_list_get(list, id2), OP_EQ, &dummy2);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 2);
  tt_int_op(list->capacity, OP_EQ, 2 * SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id2);

  for (i = id2 + 1; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  if (id3 >= SUBCIRC_LIST_MAX_CAPACITY)
    goto done;
  subcirc_list_add(list, &dummy3, id3);
  tt_ptr_op(subcirc_list_get(list, id3), OP_EQ, &dummy3);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 3);
  tt_int_op(list->capacity, OP_EQ, 16 * SUBCIRC_LIST_DEFAULT_CAPACITY);
  tt_int_op(list->max_index, OP_EQ, id3);

  for (i = id3 + 1; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  done:
  subcirc_list_free(list);
}

static void
test_subcirc_list_remove(void* arg)
{
  subcirc_list_t* list = NULL;
  subcircuit_t dummy1;
  subcircuit_t dummy2;
  subcirc_id_t id1 = 3;
  subcirc_id_t id2 = SUBCIRC_LIST_DEFAULT_CAPACITY + 2;
  unsigned int capacity;
  (void)arg;

  list = subcirc_list_new();

  capacity = list->capacity;
  subcirc_list_remove(list, 2);
  tt_int_op(list->max_index, OP_EQ, -1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 0);
  tt_int_op(list->capacity, OP_EQ, capacity);

  subcirc_list_add(list, &dummy1, id1);
  subcirc_list_add(list, &dummy2, id2);
  capacity = list->capacity;

  subcirc_list_remove(list, id2);
  tt_ptr_op(subcirc_list_get(list, id2), OP_EQ, NULL);
  tt_ptr_op(subcirc_list_get(list, id1), OP_NE, NULL);
  tt_int_op(list->max_index, OP_EQ, 3);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 1);
  tt_int_op(list->capacity, OP_EQ, capacity);

  subcirc_list_remove(list, id1);
  tt_ptr_op(subcirc_list_get(list, id1), OP_EQ, NULL);
  tt_int_op(list->max_index, OP_EQ, -1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 0);
  tt_int_op(list->capacity, OP_EQ, capacity);

  subcirc_list_remove(list, id1);
  tt_int_op(list->max_index, OP_EQ, -1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 0);
  tt_int_op(list->capacity, OP_EQ, capacity);

  for (unsigned int i = 0; i < list->capacity; i++) {
    tt_ptr_op(list->list[i], OP_EQ, NULL);
  }

  done:
  subcirc_list_free(list);
}

static void
test_subcirc_list_clear(void* arg)
{
  subcirc_list_t* list = NULL;
  subcircuit_t dummy1;
  subcircuit_t dummy2;
  subcircuit_t dummy3;
  subcirc_id_t id1 = 3;
  subcirc_id_t id2 = SUBCIRC_LIST_DEFAULT_CAPACITY + 2;
  subcirc_id_t id3 = 8 * SUBCIRC_LIST_DEFAULT_CAPACITY + 17;
  unsigned int i;
  (void)arg;

  list = subcirc_list_new();

  subcirc_list_add(list, &dummy1, id1);
  if (id2 < SUBCIRC_LIST_MAX_CAPACITY)
    subcirc_list_add(list, &dummy2, id2);
  if (id3 < SUBCIRC_LIST_MAX_CAPACITY)
    subcirc_list_add(list, &dummy3, id3);

  unsigned int capacity = list->capacity;

  subcirc_list_clear(list);

  tt_int_op(list->max_index, OP_EQ, -1);
  tt_int_op(subcirc_list_get_num(list), OP_EQ, 0);
  tt_int_op(list->capacity, OP_EQ, capacity);

  for (i = 0; i < list->capacity; i++) {
    tt_ptr_op(subcirc_list_get(list, (subcirc_id_t)i), OP_EQ, NULL);
  }

  done:
  subcirc_list_free(list);
}

static void
test_subcirc_list_contains(void* arg)
{
  subcirc_list_t* list = NULL;
  subcircuit_t dummy1;
  subcircuit_t dummy2;
  subcirc_id_t id1 = 3;
  subcirc_id_t id2 = SUBCIRC_LIST_DEFAULT_CAPACITY + 2;
  (void)arg;

  list = subcirc_list_new();

  tt_assert(!subcirc_list_contains(list, &dummy1));
  tt_assert(!subcirc_list_contains(list, &dummy2));

  subcirc_list_add(list, &dummy1, id1);

  tt_assert(subcirc_list_contains(list, &dummy1));
  tt_assert(!subcirc_list_contains(list, &dummy2));

  subcirc_list_add(list, &dummy2, id2);

  tt_assert(subcirc_list_contains(list, &dummy1));
  tt_assert(subcirc_list_contains(list, &dummy2));

  done:
  subcirc_list_free(list);
}

struct testcase_t subcirc_list_tests[] = {
  { "new",
    test_subcirc_list_new,
    0, NULL, NULL
   },
   { "free",
     test_subcirc_list_free,
     0, NULL, NULL
   },
   { "add_noresize",
     test_subcirc_list_add_noresize,
     0, NULL, NULL
   },
   { "add_resize",
     test_subcirc_list_add_resize,
     0, NULL, NULL
   },
   { "remove",
     test_subcirc_list_remove,
     0, NULL, NULL
   },
   { "clear",
     test_subcirc_list_clear,
     0, NULL, NULL
   },
   { "contains",
     test_subcirc_list_contains,
     0, NULL, NULL
   },
   END_OF_TESTCASES
};
