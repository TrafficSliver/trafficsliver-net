/**
 * \file splitutil.h
 *
 * \brief Headers for splitutil.c
 **/

#ifndef TOR_SPLITUTIL_H
#define TOR_SPLITUTIL_H

#include "core/or/or.h"
#include "feature/split/splitdefines.h"

/** Return a uint8_t that has only 0-bits left of <b>from_position</b> (excl.)
 * and only 1-bits right of position (incl). (The leftmost bit is associated
 * with position 0, the rightmost bit with position 7)
 */
static inline uint8_t
bit_mask_right(unsigned int from_position)
{
  uint8_t result = 0xFF;
  result = result >> from_position;
  return result;
}

/*** Internal functions (only use within the 'split' module) ***/
#ifdef MODULE_SPLIT_INTERNAL

const char* cpath_name(const crypt_path_t* cpath);

subcirc_id_t subcirc_id_hton(subcirc_id_t subcirc_id);
subcirc_id_t subcirc_id_ntoh(subcirc_id_t subcirc_id);
uint8_t subcirc_id_get_width(subcirc_id_t max_id);

size_t write_subcirc_id(subcirc_id_t subcirc_id, void* dest);
subcirc_id_t read_subcirc_id(const void* src);

int compare_digests(const void *a, const void *b);

#endif /* MODULE_SPLIT_INTERNAL */

#endif /* TOR_SPLITUTIL_H*  */
