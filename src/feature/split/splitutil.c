/**
 * \file splitutil.c
 *
 * \brief Utility functions used by the 'split' module
 **/

#define MODULE_SPLIT_INTERNAL
#include "feature/split/splitutil.h"

#include "core/or/or.h"
#include "core/or/crypt_path_st.h"
#include "core/or/extend_info_st.h"

#include "lib/arch/bytes.h"
#include <string.h>

/** Return a null-terminated string representation of the cryp_path_t
 * structure <b>cpath</b>.
 */
const char*
cpath_name(const crypt_path_t* cpath)
{
  static char buf[256];
  extend_info_t* info = cpath->extend_info;

  if (!info) {
    return "<unknown>";
  }

  tor_snprintf(buf, sizeof(buf), "%s (%s)",
               strlen(info->nickname) ? info->nickname : "[node]",
               hex_str(info->identity_digest, DIGEST_LEN));

  return buf;
}

/** Convert <b>subcirc_id</b> from host order to network order.
 */
subcirc_id_t subcirc_id_hton(subcirc_id_t subcirc_id)
{
  size_t size = sizeof(subcirc_id_t);
  subcirc_id_t network_order;

  tor_assert(size <= 8);

  if (size <= 1)
    network_order = subcirc_id;
  else if (size <= 2)
    network_order = (subcirc_id_t)tor_htons((uint16_t)subcirc_id);
  else if (size <= 4)
    network_order = (subcirc_id_t)tor_htonl((uint32_t)subcirc_id);
  else if (size <= 8)
    network_order = (subcirc_id_t)tor_htonll((uint64_t)subcirc_id);
  else {
    network_order = 0;
    tor_assert_unreached();
  }

  return network_order;
}

/** Convert <b>subcirc_id</b> from network order to host order.
 */
subcirc_id_t subcirc_id_ntoh(subcirc_id_t subcirc_id)
{
  size_t size = sizeof(subcirc_id_t);
  subcirc_id_t host_order = 0;

  tor_assert(size <= 8);

  if (size <= 1)
    host_order = subcirc_id;
  else if (size <= 2)
    host_order = (subcirc_id_t)tor_ntohs((uint16_t)subcirc_id);
  else if (size <= 4)
    host_order = (subcirc_id_t)tor_ntohl((uint32_t)subcirc_id);
  else if (size <= 8)
    host_order = (subcirc_id_t)tor_ntohll((uint64_t)subcirc_id);
  else {
    host_order = 0;
    tor_assert_unreached();
  }

  return host_order;
}

/** Return the number of bits needed to encode sub-circuit IDs which
 * are smaller than or equal to <b>max_id<b>.
 */
uint8_t subcirc_id_get_width(subcirc_id_t max_id)
{
  uint8_t width;

  if (max_id == 0)
    /* we always need to use at least 1 bit for encoding */
    return 1;

  width = 0;
  while (max_id != 0) {
    max_id = max_id >> 1;
    width++;
  }

  return width;
}

/** Write the <b>subcirc_id</b> to the buffer <b>dest</b>.
 * Return the number of bytes written (equals
 * sizeof(subcircuit_id_t).
 */
size_t
write_subcirc_id(subcirc_id_t subcirc_id, void* dest)
{
  size_t size = sizeof(subcirc_id_t);

  tor_assert(dest);
  tor_assert(size <= 8);

  memcpy(dest, &subcirc_id, size);
  return size;
}

/** Read the subcirc_id from buffer <b>src</b>.
 * (Automatically determine size to read by
 * sizeof(subcirc_id_t)).
 */
subcirc_id_t
read_subcirc_id(const void* src)
{
  size_t size = sizeof(subcirc_id_t);
  subcirc_id_t result;

  tor_assert(src);
  tor_assert(size <= 8);

  memcpy(&result, src, size);
  return result;
}

/** Helper: compare two DIGEST_LEN digests.
 * (Stolen from smartlist.c) */
int
compare_digests(const void *a, const void *b)
{
  return tor_memeq((const char*)a, (const char*)b, DIGEST_LEN);
}
