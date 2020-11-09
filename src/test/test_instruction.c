#define MODULE_SPLIT_INTERNAL
#define TOR_SPLITSTRATEGY_PRIVATE
#include "core/or/or.h"
#include "test/test.h"

#include "feature/split/splitstrategy.h"
#include "feature/split/splitutil.h"

static void
test_instruction_get_width(void* arg)
{
  (void)arg;

  tt_uint_op(subcirc_id_get_width(0), OP_EQ, 1);
  tt_uint_op(subcirc_id_get_width(1), OP_EQ, 1);
  tt_uint_op(subcirc_id_get_width(3), OP_EQ, 2);
  tt_uint_op(subcirc_id_get_width(16), OP_EQ, 5);
  tt_uint_op(subcirc_id_get_width(255), OP_EQ, 8);
  tt_uint_op(subcirc_id_get_width(1 << 10), OP_EQ, 11);

  done:
  return;
}

static void
test_instruction_parse_to_payload_generic1(void* arg)
{
  subcirc_id_t IDs[] = {3, 1, 3, 0, 2};
  size_t length = sizeof(IDs);
  uint8_t* payload = NULL;
  ssize_t payload_len;
  (void)arg;

  payload_len = parse_to_payload_generic(IDs, length, &payload);

  /* max_id is 3, so width should be 2; total_bits will be 2 * 5 = 10,
   * so we need payload_len of 2 + 2 (with 2 header bytes) with
   * empty_bits == 6 */
  tt_int_op(payload_len, OP_EQ, 4);
  tt_ptr_op(payload, OP_NE, NULL);

  tt_uint_op(payload[0], OP_EQ, SPLIT_INSTRUCTION_TYPE_GENERIC);
  tt_uint_op(payload[1], OP_EQ, 0b00010110); /* width (5) | empty_bits (3) */

  tt_uint_op(payload[2], OP_EQ, 0b11011100); /* 3, 1, 3, 0 */
  tt_uint_op(payload[3], OP_EQ, 0b10000000); /* 2 (6 empty) */

  done:
  tor_free(payload);
}

static void
test_instruction_parse_to_payload_generic2(void* arg)
{
  subcirc_id_t IDs[] = {0, 6, 5, 0, 1, 3, 4, 2, 1};
  size_t length = sizeof(IDs);
  uint8_t* payload = NULL;
  ssize_t payload_len;
  (void)arg;

  payload_len = parse_to_payload_generic(IDs, length, &payload);

  /* max_id is 6, so width should be 3; total_bits will be 3 * 9 = 27,
   * so we need payload_len of 4 + 2 (with 2 header bytes) with
   * empty_bits == 5 */
  tt_int_op(payload_len, OP_EQ, 6);
  tt_ptr_op(payload, OP_NE, NULL);

  tt_uint_op(payload[0], OP_EQ, SPLIT_INSTRUCTION_TYPE_GENERIC);
  tt_uint_op(payload[1], OP_EQ, 0b00011101); /* width (5) | empty_bits (3) */

  tt_uint_op(payload[2], OP_EQ, 0b00011010); /* 0, 6, 5a */
  tt_uint_op(payload[3], OP_EQ, 0b10000010); /* 5b, 0, 1, 3a */
  tt_uint_op(payload[4], OP_EQ, 0b11100010); /* 3b, 4, 2 */
  tt_uint_op(payload[5], OP_EQ, 0b00100000); /* 1 (5 empty) */

  done:
  tor_free(payload);
}

static void
test_instruction_parse_to_payload_generic3(void* arg)
{
  subcirc_id_t IDs[] = {
      (1 << 12) | (1 << 6),
      (1 << 11) | (1 << 2),
      (1 << 12) | (1 << 10) | 1
  };
  size_t length = sizeof(IDs);
  uint8_t* payload = NULL;
  ssize_t payload_len;
  (void)arg;

  payload_len = parse_to_payload_generic(IDs, length, &payload);

  /* width should be 13; total_bits will be 13 * 3 = 39,
   * so we need payload_len of 5 + 2 (with 2 header bytes) with
   * empty_bits == 1 */
  tt_int_op(payload_len, OP_EQ, 7);
  tt_ptr_op(payload, OP_NE, NULL);

  tt_uint_op(payload[0], OP_EQ, SPLIT_INSTRUCTION_TYPE_GENERIC);
  tt_uint_op(payload[1], OP_EQ, 0b001101001); /* width (5) | empty_bits (3) */

  tt_uint_op(payload[2], OP_EQ, 0b10000010); /* ID1 (8) */
  tt_uint_op(payload[3], OP_EQ, 0b00000010); /* ID1 (5) | ID2 (3)*/
  tt_uint_op(payload[4], OP_EQ, 0b00000001); /* ID2 (8) */
  tt_uint_op(payload[5], OP_EQ, 0b00101000); /* ID2 (2) | ID3 (6) */
  tt_uint_op(payload[6], OP_EQ, 0b00000010); /* ID3 (7) (1 empty) */

  done:
  tor_free(payload);
}

static void
test_instruction_parse_from_payload_generic1(void* arg)
{
  uint8_t payload[] = {
      SPLIT_INSTRUCTION_TYPE_GENERIC,
      0b00101100, /* width (5) | empty_bits (3) */
      0b00000000, /* 0, 3a */
      0b11111111, /* 3b, 31, 17a */
      0b00010000 /* 17b (4 empty)*/
  };
  size_t payload_len = sizeof(payload);
  subcirc_id_t* list = NULL;
  ssize_t length;
  (void)arg;

  length = parse_from_payload_generic(payload, payload_len, &list);

  tt_int_op(length, OP_EQ, 4 * sizeof(subcirc_id_t));
  tt_ptr_op(list, OP_NE, NULL);

  tt_uint_op(read_subcirc_id(list + 0), OP_EQ, 0);
  tt_uint_op(read_subcirc_id(list + 1), OP_EQ, 3);
  tt_uint_op(read_subcirc_id(list + 2), OP_EQ, 31);
  tt_uint_op(read_subcirc_id(list + 3), OP_EQ, 17);

  done:
  tor_free(list);
}

static void
test_instruction_parse_from_payload_generic2(void* arg)
{
  uint8_t payload[] = {
      SPLIT_INSTRUCTION_TYPE_GENERIC,
      0b01111010, /* width = 15 | empty_bits = 2 */
      0b11001100,
      0b00110010,
      0b11011111,
      0b01011011,
  };
  size_t payload_len = sizeof(payload);
  subcirc_id_t* list = NULL;
  ssize_t length;
  (void)arg;

  length = parse_from_payload_generic(payload, payload_len, &list);

  tt_int_op(length, OP_EQ, 2 * sizeof(subcirc_id_t));
  tt_ptr_op(list, OP_NE, NULL);

  tt_uint_op(read_subcirc_id(list + 0), OP_EQ, 0b110011000011001);
  tt_uint_op(read_subcirc_id(list + 1), OP_EQ, 0b011011111010110);

  done:
  tor_free(list);
}

static void
test_instruction_parse_generic1(void* arg)
{
  subcirc_id_t IDs[] = {
      17, 89, 32, 100, 65535, 10000, 0, 0, 62, 42, 381, 56, 74, 90, 42424, 987
  };
  size_t length1 = sizeof(IDs);
  ssize_t payload_len, length2;
  uint8_t* payload = NULL;
  subcirc_id_t* list = NULL;
  (void)arg;

  payload_len = parse_to_payload_generic(IDs, length1, &payload);

  tt_int_op(payload_len, OP_GT, 0);
  tt_ptr_op(payload, OP_NE, NULL);

  length2 = parse_from_payload_generic(payload, payload_len, &list);

  tt_int_op(length2, OP_EQ, (ssize_t)length1);
  tt_ptr_op(list, OP_NE, NULL);

  for (size_t pos = 0; pos < length1 / sizeof(subcirc_id_t); pos++) {
    tt_uint_op(IDs[pos], OP_EQ, read_subcirc_id(list + pos));
  }

  done:
  tor_free(payload);
  tor_free(list);
}

struct testcase_t instruction_tests[] = {
  { "get_width",
    test_instruction_get_width,
    0, NULL, NULL
  },
  { "parse_to_payload_generic1",
    test_instruction_parse_to_payload_generic1,
    0, NULL, NULL
  },
  { "parse_to_payload_generic2",
    test_instruction_parse_to_payload_generic2,
    0, NULL, NULL
  },
  { "parse_to_payload_generic3",
    test_instruction_parse_to_payload_generic3,
    0, NULL, NULL
  },
  { "parse_from_payload_generic1",
    test_instruction_parse_from_payload_generic1,
    0, NULL, NULL
  },
  { "parse_from_payload_generic2",
    test_instruction_parse_from_payload_generic2,
    0, NULL, NULL
  },
  { "parse_generic1",
    test_instruction_parse_generic1,
    0, NULL, NULL
  },
  END_OF_TESTCASES
};
