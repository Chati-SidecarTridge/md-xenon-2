/**
 * File: crc32.h
 * Description: CRC-32 (the polynomial of zlib, PNG and Ethernet), updated a
 *              piece at a time. Plain C, no SDK.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* crc32_update(0, data, n) is the CRC-32 of data; feed it back to go on. */
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
