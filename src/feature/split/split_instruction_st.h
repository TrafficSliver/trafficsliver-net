/**
 * \file split_instruction_st.h
 *
 * \brief Definition of the split_instruction_t data structure
 *
 **/

#ifndef TOR_SPLIT_INSTRUCTION_H
#define TOR_SPLIT_INSTRUCTION_H


#include "feature/split/splitdefines.h"

struct split_instruction_t {

  /** Pointer to the next split instruction */
  split_instruction_t* next;

  /** Type of this split instruction */
  instruction_type_t type;

  /** (Parsed) instruction data (data type might be different for
   * different instruction types) */
  void* data;

  /** Offset/pointer to the currently relevant position within data
   * (position == 0 points to the beginning of data) */
  size_t position;

  /** Length of the memory block referenced by data */
  size_t length;

};

#endif /* TOR_SPLIT_INSTRUCTION_H */
