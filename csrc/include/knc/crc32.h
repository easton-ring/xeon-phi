#ifndef KNC_CRC32_H
#define KNC_CRC32_H
#include <stddef.h>
#include <stdint.h>

/* zlib-compatible CRC-32. Start with crc = 0; chain calls over consecutive buffers. */
uint32_t knc_crc32_update(uint32_t crc, const void *buf, size_t len);

#endif
