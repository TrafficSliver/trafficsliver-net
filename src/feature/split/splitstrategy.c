/**
 * \file splitstrategy.c
 *
 * \brief Implementation of splitting strategies and instructions.
 **/

#define MODULE_SPLIT_INTERNAL
#define TOR_SPLITSTRATEGY_PRIVATE
#include "feature/split/splitstrategy.h"
#include "app/config/config.h"
#include "core/or/or.h"
#include "feature/split/splitutil.h"
#include "feature/split/split_instruction_st.h"
#include "feature/split/subcirc_list.h"

#include "feature/split/dirichlet/mydirichlet.h" //My dirichlet implementation
#include "src/lib/math/fp.h"

#include "lib/crypt_ops/crypto_rand.h"

/** Allocate a new split_instruction_t structure and return a pointer
 */
split_instruction_t*
split_instruction_new(void)
{
  split_instruction_t* inst;
  inst = tor_malloc_zero(sizeof(split_instruction_t));

  /* initialisation of struct members */

  return inst;
}

/** Deallocate the memory associated with <b>inst</b>
 */
void
split_instruction_free_(split_instruction_t* inst)
{
  if (!inst)
    return;

  /* deinitialisation of struct members */
  tor_free(inst->data);

  tor_free(inst);
}

/** Helper for parsing the <b>payload</b> (of length <b>payload_len</b>)
 * of a generic split instruction cell to a list of subcirc_id_t <b>data</b>.
 * Returns the length of data on success or -1 on error.
 */
STATIC ssize_t
parse_from_payload_generic(const uint8_t* payload, size_t payload_len,
                           subcirc_id_t** data)
{
  uint8_t width, empty_bits, remaining;
  unsigned int bits_read, curr_byte, curr_bit;
  size_t total_bits, num, count;

  tor_assert(payload);
  tor_assert(payload_len <= RELAY_PAYLOAD_SIZE);
  tor_assert(data);

  if (BUG(payload_len < 3)) {
    /* 1 byte instruction + 1 byte width/empty_bits + at least 1 byte IDs */
    log_warn(LD_CIRC, "Payload too short (%zu bytes)", payload_len);
    return -1;
  }

  if (BUG(payload[0] != SPLIT_INSTRUCTION_TYPE_GENERIC)) {
    log_warn(LD_CIRC, "Instruction type not correct.");
    return -1;
  }

  width = (payload[1] & 0xF8) >> 3; /* first 5 bits */
  empty_bits = payload[1] & 0x07; /* last 3 bits */

  if (BUG(width == 0))
    /* width must be positive */
    return -1;

  payload = payload + 2;
  payload_len = payload_len - 2;

  total_bits = payload_len * 8;
  tor_assert(empty_bits < 8);
  tor_assert(total_bits > empty_bits);
  total_bits -= empty_bits;

  if (BUG(total_bits % width != 0))
    /* wrong alignment */
    return -1;

  num = total_bits / width;
  *data = tor_malloc_zero(num * sizeof(subcirc_id_t));

  count = 0;
  bits_read = 0;
  curr_byte = 0;
  curr_bit = 0;
  while (bits_read < total_bits) {
    tor_assert(bits_read + width <= total_bits);

    subcirc_id_t current_id = 0;
    tor_assert(curr_byte == bits_read / 8);
    tor_assert(curr_bit == bits_read % 8);
    remaining = width;

    if (curr_bit + remaining > 8) {
      current_id |= payload[curr_byte] & bit_mask_right(curr_bit);
      remaining = remaining + curr_bit - 8;
      curr_byte++;
      curr_bit = 0;
    }

    while (remaining && curr_bit + remaining > 8) {
      tor_assert(curr_bit == 0);
      current_id = current_id << 8;
      current_id |= payload[curr_byte];
      remaining = remaining - 8;
      curr_byte++;
    }

    if (remaining) {
      tor_assert(curr_bit + remaining <= 8);
      current_id = current_id << remaining;
      current_id |= (payload[curr_byte] & bit_mask_right(curr_bit)) >>
          (8 - (curr_bit + remaining));
      curr_bit += remaining;
      if (curr_bit == 8) {
        curr_bit = 0;
        curr_byte++;
      }
    }

    tor_assert(count < num);
    write_subcirc_id(current_id, *data + count);

    count++;
    bits_read += width;
  }

  tor_assert(count == num);

  return (ssize_t)num * sizeof(subcirc_id_t);
}

/** Helper for parsing a list of subcirc_id_t <b>data</b> of
 * length <b>data_len</b> into a <b>payload</b> which can be
 * used for split instruction cells. Returns the length of
 * payload on success or -1 on error.
 */
STATIC ssize_t
parse_to_payload_generic(const subcirc_id_t* data, size_t data_len,
                         uint8_t** payload)
{
  subcirc_id_t max_id;
  uint8_t width, empty_bits, remaining;
  unsigned int written_bits, curr_byte, curr_bit;
  size_t num, count, length, total_bits;
  uint8_t* payload_IDs;

  tor_assert(data);
  tor_assert(data_len > 0);

  tor_assert(data_len % sizeof(subcirc_id_t) == 0);
  num = data_len / sizeof(subcirc_id_t);

  /* find maximum index and determine width and empty_bits */
  max_id = 0;
  for (size_t pos = 0; pos < num; pos++) {
    subcirc_id_t id = read_subcirc_id(data + pos);
    if (id > max_id)
      max_id = id;
  }

  width = subcirc_id_get_width(max_id);

  if (BUG(width >= (1 << 6))) {
    log_warn(LD_CIRC, "Width is too big (%u). How is this possible?", width);
    return -1;
  }

  tor_assert(width <= 8 * sizeof(subcirc_id_t));

  total_bits = num * width;
  empty_bits = total_bits % 8 ? 8 - (total_bits % 8) : 0;
  length = total_bits / 8;
  length += empty_bits ? 1 : 0; /* for half-full byte at the end */
  length += 2;  /* for type, width, empty_bits fields */

  if (BUG(length > RELAY_PAYLOAD_SIZE)) {
    log_warn(LD_CIRC, "Too much payload for split instruction cell "
             "(%zu bytes; allowed are %d bytes)", length, RELAY_PAYLOAD_SIZE);
    return -1;
  }

  *payload = tor_malloc_zero(length);
  (*payload)[0] = SPLIT_INSTRUCTION_TYPE_GENERIC;
  (*payload)[1] |= width << 3; /* first 5 bits */
  (*payload)[1] |= empty_bits & 0x07; /* last 3 bits */

  payload_IDs = *payload + 2;

  count = 0;
  written_bits = 0;
  curr_byte = 0;
  curr_bit = 0;
  while (count < num) {
    subcirc_id_t current_id = read_subcirc_id(data + count);

    tor_assert(curr_byte == written_bits / 8);
    tor_assert(curr_bit == written_bits % 8);
    tor_assert(curr_byte < length - 2);
    remaining = width;

    if (curr_bit + remaining > 8) {
      payload_IDs[curr_byte] |=
          (current_id  >> (remaining + curr_bit - 8)) &
          bit_mask_right(curr_bit);
      remaining = remaining + curr_bit - 8;
      curr_byte++;
      curr_bit = 0;
    }

    while (remaining && curr_bit + remaining > 8) {
      tor_assert(curr_bit == 0);
      payload_IDs[curr_byte] = current_id >> (remaining - 8);
      remaining = remaining - 8;
      curr_byte++;
    }

    if (remaining) {
      tor_assert(curr_bit + remaining <= 8);
      payload_IDs[curr_byte] |= current_id << (8 - (curr_bit + remaining)) &
          bit_mask_right(curr_bit);
      curr_bit += remaining;
      if (curr_bit == 8) {
        curr_bit = 0;
        curr_byte++;
      }
    }

    count++;
    written_bits += width;
  }

  tor_assert(written_bits == total_bits);

  return length;
}

/** Parse the <b>payload</b> of a split instruction cell
 * (RELAY_COMMAND_SPLIT_INSTRUCTION or RELAY_COMMAND_SPLIT_INFO) into a new
 * split_instruction_t structure.
 */
split_instruction_t*
split_payload_to_instruction(size_t length, const uint8_t* payload)
{
  split_instruction_t* inst;
  instruction_type_t type;
  ssize_t data_len = 0;

  tor_assert(payload);
  if (length < 1) {
    log_warn(LD_CIRC, "Payload too short (%zu bytes)", length);
    return NULL;
  }

  inst = split_instruction_new();
  type = (instruction_type_t)payload[0];

  switch (type) {
    case SPLIT_INSTRUCTION_TYPE_GENERIC:
      data_len = parse_from_payload_generic(payload, length,
                                            (subcirc_id_t**)(&(inst->data)));
      break;
    default:
      log_warn(LD_CIRC, "Unrecognized instruction type %d", type);
      return NULL;
  }

  if (data_len < 0) {
    log_warn(LD_CIRC, "Could not parse payload to split instruction");
    return NULL;
  }

  tor_assert(data_len != 0);
  inst->length = (size_t)data_len;
  inst->type = type;
  return inst;
}

/** Parse the the split_instruction_t struct <b>inst</b> into a <b>payload</b>
 * which can be used for split instruction cells.
 */
ssize_t
split_instruction_to_payload(const split_instruction_t* inst,
                             uint8_t** payload)
{
  ssize_t length = 0;

  tor_assert(inst);
  tor_assert(inst->data);
  tor_assert(inst->length > 0);
  tor_assert(payload);

  switch (inst->type) {
    case SPLIT_INSTRUCTION_TYPE_GENERIC:
      length = parse_to_payload_generic(inst->data, inst->length, payload);
      break;
    default:
      log_warn(LD_CIRC, "Unrecognized instruction type %d", inst->type);
      tor_assert_unreached();
      return -1;
  }

  if (BUG(length < 0)) {
    log_warn(LD_CIRC, "Could not parse split instruction to payload");
    length = -1;
  } else {
    tor_assert(*payload);
  }

  return length;
}

/** Return the maximum number of sub-circuit IDs (based on <b>max_id</b>)
 * that can be fitted into the payload of a generic split instruction cell.
 */
static int
get_max_ids_generic(subcirc_id_t max_id)
{
  size_t max_data_len = RELAY_PAYLOAD_SIZE - 2; /* 2 "header" bits */
  size_t total_bits = max_data_len * 8;
  uint8_t width = subcirc_id_get_width(max_id);
  tor_assert(width != 0);

  return (int)(total_bits / width);
}

/* Return a new split_instruction_t instance based following the
 * MIN_ID strategy (based on the given list of <b>subcircs</b> and
 * cell <b>direction</b>.
 */
static split_instruction_t*
get_instruction_min_id(subcirc_list_t* subcircs, cell_direction_t direction)
{
  split_instruction_t* inst;
  int num;
  subcirc_id_t* list;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  /* if a subcirc_list is not empty, the minimum index must always be 0 */
  tor_assert(subcirc_list_get(subcircs, 0));

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;

  num = get_max_ids_generic(0);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));

  /* fill list with minimum sub-circuit ID, which is 0 */
  for (int pos = 0; pos < num; pos++) {
    write_subcirc_id(0, list + pos);
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

/* Return a new split_instruction_t instance based following the
 * MAX_ID strategy (based on the given list of <b>subcircs</b> and
 * cell <b>direction</b>.
 */
static split_instruction_t*
get_instruction_max_id(subcirc_list_t* subcircs, cell_direction_t direction)
{
  split_instruction_t* inst;
  subcirc_id_t max_id;
  int num;
  subcirc_id_t* list;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  tor_assert(subcircs->max_index >= 0);

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;

  max_id = (subcirc_id_t)subcircs->max_index;
  num = get_max_ids_generic(max_id);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));

  /* fill list with minimum sub-circuit ID, which is 0 */
  for (int pos = 0; pos < num; pos++) {
    write_subcirc_id(max_id, list + pos);
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

static split_instruction_t*
get_instruction_round_robin(subcirc_list_t* subcircs,
                            cell_direction_t direction)
{
  split_instruction_t* inst;
  subcirc_id_t max_id;
  int num;
  subcirc_id_t* list;
  subcirc_id_t current_id;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  tor_assert(subcircs->max_index >= 0);

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;
  max_id = (subcirc_id_t)subcircs->max_index;
  num = get_max_ids_generic(max_id);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));

  current_id = 0;
  tor_assert(subcirc_list_get(subcircs, 0));
  /* fill list in an round robin manner */
  for (int pos = 0; pos < num; pos++) {
    write_subcirc_id(current_id, list + pos);
    do {
      current_id = (current_id + 1) % (unsigned int)(max_id + 1);
    } while (!subcirc_list_get(subcircs, current_id));
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

static split_instruction_t*
get_instruction_random_uniform(subcirc_list_t* subcircs,
                               cell_direction_t direction)
{
  split_instruction_t* inst;
  subcirc_id_t max_id;
  int num;
  subcirc_id_t* list;
  subcirc_id_t current_id;
  subcirc_id_t random;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  tor_assert(subcircs->max_index >= 0);

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;
  max_id = (subcirc_id_t)subcircs->max_index;
  num = get_max_ids_generic(max_id);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));

  /* fill list with random subcird_ids */
  for (int pos = 0; pos < num; pos++) {

    do {
      crypto_rand((char*)&random, sizeof(subcirc_id_t));
      current_id = random % (unsigned int)(max_id + 1);
    } while (!subcirc_list_get(subcircs, current_id));

    write_subcirc_id(current_id, list + pos);
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

/*wdlc Weighted Random implementation*/
static split_instruction_t*
get_instruction_weighted_random(subcirc_list_t* subcircs,
                               cell_direction_t direction, 
                               int use_prev,
                               double *prev_data)
{
  split_instruction_t* inst;
  subcirc_id_t max_id;
  int num;
  subcirc_id_t* list;
  subcirc_id_t current_id;
  //subcirc_id_t random;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  tor_assert(subcircs->max_index >= 0);

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;
  max_id = (subcirc_id_t)subcircs->max_index;
  num = get_max_ids_generic(max_id);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));
  // Using the dirichlet distribution to create m weights for the random choice
  int number_of_paths = max_id + 1;
  double alpha[number_of_paths];
  double theta[number_of_paths];

  for (int k = 0 ; k < number_of_paths ; k ++){
  	alpha[k] = 1;
  	theta[k] = 1; // Standard dirichlet has alpha values equal to 1
  }

  if (use_prev == 0){ //wdlc: if == 0 this is the beginning of a page load, let me store the weights for later within the current page load
    gsl_rng * r;
    r=gsl_rng_alloc(gsl_rng_mt19937);
    // Creating always a different seed
    struct timeval tv; 
    gettimeofday(&tv,0);
    unsigned long mySeed = tv.tv_sec + tv.tv_usec;
    gsl_rng_set(r, mySeed);
    ran_dirichlet(r, max_id +1 ,alpha,theta);
    gsl_rng_free(r);
    log_info (LD_CIRC, "Weight vector %f,%f,%f", 100*(theta[0]), 100*(theta[1]), 100*(theta[2]) );
    for (int k = 0 ; k < number_of_paths ; k++){
         prev_data[k] = theta[k];
         //log_info(LD_CIRC, "storing this dirichlet vector for later too theta j %f %f ", (double)inst->distribution_data[k], (double)theta[k]);
    }
  }

  if (use_prev == 1){ //wdlc: if == 1 this is inside the samea page load, let me use the previously-stored values of the dirichlet vector
    for (int k = 0 ; k < number_of_paths ; k++){
       theta[k] = prev_data[k] ;
       log_info(LD_CIRC, "I do not finish the page load use the same weight vector %f ", theta[k]);
    }
  }
  // Create a weighted vector with the indexes of the available paths
  unsigned int weighted_paths[100] = {number_of_paths-1};
  int last_index = 0;

  for (int j = 0; j < number_of_paths ; j++){
      int max_subindex = (int) tor_lround(100*theta[j]);
      log_info (LD_CIRC, "number of circuit %i, %i limits %i,%i", j, max_subindex, last_index, max_subindex);
      for (int g = 0; g < (max_subindex) ; g++){
      	weighted_paths[g + last_index] = j;
      }
      last_index = max_subindex + last_index;
  }
  /* fill list with random subcird_ids biased by a weighted vector*/
  for (int pos = 0; pos < num; pos++) {
    do {
      int random_int = crypto_rand_int_range(0,100);
      int random_weighted_circuit_index = weighted_paths[random_int];
      current_id = random_weighted_circuit_index;
    } while (!subcirc_list_get(subcircs, current_id));

    write_subcirc_id(current_id, list + pos);
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

/*wdlc Batched Weighted Random implementation*/
static split_instruction_t*
get_instruction_batched_weighted_random(subcirc_list_t* subcircs,
                               cell_direction_t direction,
                               int use_prev,
                               double *prev_data)
{
  split_instruction_t* inst;
  subcirc_id_t max_id;
  int num;
  subcirc_id_t* list;
  subcirc_id_t current_id;
  //subcirc_id_t random;
  (void)direction;
  tor_assert(subcircs);

  tor_assert(subcirc_list_get_num(subcircs) > 0);
  tor_assert(subcircs->max_index >= 0);

  inst = split_instruction_new();
  inst->type = SPLIT_INSTRUCTION_TYPE_GENERIC;
  max_id = (subcirc_id_t)subcircs->max_index;
  num = get_max_ids_generic(max_id);
  list = tor_malloc_zero(num * sizeof(subcirc_id_t));
  // Using the dirichlet distribution to create m weights for the random choice
  int number_of_paths = max_id + 1;
  double alpha[number_of_paths];
  double theta[number_of_paths];
  for (int k = 0 ; k < number_of_paths ; k ++){
  	alpha[k] = 1;
  	theta[k] = 1; // Standard dirichlet has alpha values equal to 1
  }
  if (use_prev == 0){ //wdlc: if == 0 this is the beginning of a page load, let me calculate & store the weights for later within the current page load
    gsl_rng * r;
    r=gsl_rng_alloc(gsl_rng_mt19937);
    //Creating always a different seed
    struct timeval tv;
    gettimeofday(&tv,0);
    unsigned long mySeed = tv.tv_sec + tv.tv_usec;
    gsl_rng_set(r, mySeed);
    ran_dirichlet(r, max_id +1 ,alpha,theta);
    gsl_rng_free(r);
    log_info (LD_CIRC, "BWR Weight vector %f,%f,%f", 100*(theta[0]), 100*(theta[1]), 100*(theta[2]) );
    for (int k = 0 ; k < number_of_paths ; k++){
       prev_data[k] = theta[k];
       //log_info(LD_CIRC, "storing this dirichlet vector for later too theta j %f %f ", (double)inst->distribution_data[k], (double)theta[k]);
    }
  }

  if (use_prev == 1){ //wdlc: if == 1 this is inside the samea page load, let me use the previously-stored values of the dirichlet vector
    for (int k = 0 ; k < number_of_paths ; k++){
       theta[k] = prev_data[k] ;
       log_info(LD_CIRC, "I do not finish the page load use the same weight vector %f ", theta[k]);
    }
   }

  // Create a weighted vector with the indexes of the available paths
  unsigned int weighted_paths[100] = {number_of_paths-1};
  int last_index = 0;

  for (int j = 0; j < number_of_paths ; j++){
      int max_subindex = (int) tor_lround(100*theta[j]);
      log_info (LD_CIRC, "number of circuit %i, %i limits %i,%i", j, max_subindex, last_index, max_subindex);
      for (int g = 0; g < (max_subindex) ; g++){
      	weighted_paths[g + last_index] = j;
      }
      last_index = max_subindex + last_index;
  }
  current_id =  weighted_paths[crypto_rand_int_range(0,100)]; // First route is weighted random chosen
  /* fill list with random subcird_ids biased by a weighted vector*/
  for (int pos = 0; pos < num; pos++) {
    do {
      int current_batch_size = crypto_rand_int_range(C_MIN, C_MAX);
      if ((pos % current_batch_size)==0){
      	current_id = weighted_paths[crypto_rand_int_range(0,100)]; // After the batch size, perform a new weighted random choice
      }
    } while (!subcirc_list_get(subcircs, current_id));

    write_subcirc_id(current_id, list + pos);
  }

  inst->data = list;
  inst->length = num * sizeof(subcirc_id_t);

  return inst;
}

/** Return a new split_instruction_t instance based on the given
 * <b>strategy</b>, list of <b>subcircs</b> and cell <b>direction</b>.
 */
split_instruction_t*
split_get_new_instruction(split_strategy_t strategy,
                          subcirc_list_t* subcircs,
                          cell_direction_t direction,
                          int use_prev,
                          double* prev_data)
{
  split_instruction_t* inst = NULL;
  tor_assert(subcircs);

  switch (strategy) {
    case SPLIT_STRATEGY_MIN_ID:
      inst = get_instruction_min_id(subcircs, direction);
      break;
    case SPLIT_STRATEGY_MAX_ID:
      inst = get_instruction_max_id(subcircs, direction);
      break;
    case SPLIT_STRATEGY_ROUND_ROBIN:
      inst = get_instruction_round_robin(subcircs, direction);
      break;
    case SPLIT_STRATEGY_RANDOM_UNIFORM:
      inst = get_instruction_random_uniform(subcircs, direction);
      break;
    case SPLIT_STRATEGY_WEIGHTED_RANDOM:
      inst = get_instruction_weighted_random(subcircs, direction, use_prev, prev_data);
      break;
    case SPLIT_STRATEGY_BATCHED_WEIGHTED_RANDOM:
      inst = get_instruction_batched_weighted_random(subcircs, direction, use_prev, prev_data);
      break;
    default:
      tor_assert_unreached();
  } 
  tor_assert(inst);
  return inst;
}

 /** Return how many cells can be sent with the instruction, 
     in other words how many indexes are left to follow
 */

//int split_instruction_get_left_instructions(split_instruction_t** inst_ptr)
//{
//  split_instruction_t* inst = *inst_ptr;
//  //subcirc_id_t next_id;
//  tor_assert(inst);
//  tor_assert(inst->data);
//  tor_assert(inst->position < inst->length);
//  switch (inst->type) {
//    case SPLIT_INSTRUCTION_TYPE_GENERIC:
//      tor_assert(inst->position + sizeof(subcirc_id_t) <= inst->length);
//      //inst->position += sizeof(subcirc_id_t);
//      return (inst->length - inst->position);
//      break;
//   }
//}

/** Return the ID of the next sub-circuit as defined by the
 * split <b>inst</b>ruction. When the end of inst->data is
 * reached (as indicated by inst->position), inst will be
 * replaced with inst->next.
 */
subcirc_id_t
split_instruction_get_next_id(split_instruction_t** inst_ptr)
{
  split_instruction_t* inst = *inst_ptr;
  subcirc_id_t next_id;
  tor_assert(inst);
  tor_assert(inst->data);
  tor_assert(inst->position < inst->length);
  switch (inst->type) {
    case SPLIT_INSTRUCTION_TYPE_GENERIC:
      tor_assert(inst->position + sizeof(subcirc_id_t) <= inst->length);
      next_id = read_subcirc_id((uint8_t*)inst->data + inst->position);
      inst->position += sizeof(subcirc_id_t);
      if (inst->position >= inst->length) {
        *inst_ptr = inst->next;
        split_instruction_free(inst);
      }
      break;
    default:
      tor_assert_unreached();
  }

  return next_id;
}

/** Append a <b>new</b> split instruction to the end of the
 * single-linked list <b>existing</b>.
 */
void
split_instruction_append(split_instruction_t** existing,
                         split_instruction_t* new)
{
  split_instruction_t* iterator;
  tor_assert(existing);

  if (!*existing) {
    *existing = new;
    return;
  }

  iterator = *existing;
  while (iterator->next) {
    iterator = iterator->next;
  }

  iterator->next = new;
}

/** Return the length of the single-linked list of split
 * instructions that begins at <b>list</b>.
 */
int
split_instruction_list_length(split_instruction_t* list)
{
  int length = 0;

  while (list) {
    length += 1;
    list = list->next;
  }

  return length;
}

/** Check, if the given split <b>inst</b>ruction only refers to sub-circuit
 * IDs that are known to the sub-circuit list <b>subcircs</b>.
 * Return TRUE on success, FALSE on failure.
 */
int
split_instruction_check(split_instruction_t* inst, subcirc_list_t* subcircs)
{
  tor_assert(inst);
  tor_assert(subcircs);

  switch (inst->type) {
    case SPLIT_INSTRUCTION_TYPE_GENERIC:
      if (BUG(inst->length == 0)) return -1;
      for (size_t pos = 0; pos < inst->length; pos += sizeof(subcirc_id_t)) {
        subcirc_id_t id = read_subcirc_id((uint8_t*)inst->data + pos);
        if (BUG(!subcirc_list_get(subcircs, id))) return 0;
      }
      break;
    default:
      tor_assert_nonfatal_unreached();
      return 0;
  }

  return 1;
}

/** Free a whole single-linked <b>list</b> of split instructions.
 */
void
split_instruction_free_list(split_instruction_t** list)
{
  split_instruction_t *victim, *iterator;
  tor_assert(list);

  iterator = *list;
  while (iterator) {
    victim = iterator;
    iterator = iterator->next;
    split_instruction_free(victim);
  }

  *list = NULL;
}

/** Return the default split_strategy to be used by new split circuits. */
split_strategy_t
split_get_default_strategy(void)
{
  const or_options_t* options = get_options();

  if (!options->SplitStrategy)
    return SPLIT_DEFAULT_STRATEGY;
  else if (!strcmp(options->SplitStrategy, "MIN_ID"))
    return SPLIT_STRATEGY_MIN_ID;
  else if (!strcmp(options->SplitStrategy, "MAX_ID"))
    return SPLIT_STRATEGY_MAX_ID;
  else if (!strcmp(options->SplitStrategy, "ROUND_ROBIN"))
    return SPLIT_STRATEGY_ROUND_ROBIN;
  else if (!strcmp(options->SplitStrategy, "RANDOM_UNIFORM"))
    return SPLIT_STRATEGY_RANDOM_UNIFORM;
  else if (!strcmp(options->SplitStrategy, "WEIGHTED_RANDOM"))
    return SPLIT_STRATEGY_WEIGHTED_RANDOM;
  else if (!strcmp(options->SplitStrategy, "BATCHED_WEIGHTED_RANDOM"))
    return SPLIT_STRATEGY_BATCHED_WEIGHTED_RANDOM;

  else
    return SPLIT_DEFAULT_STRATEGY;
}
