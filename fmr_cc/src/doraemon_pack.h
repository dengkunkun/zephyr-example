/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file doraemon_pack.h
 * @brief Byte-order conversion helpers — straight port of original doraemon_pack.
 *
 * Provides pack/unpack helpers for converting between byte arrays and
 * multi-byte integers in either LSB-first (little-endian) or MSB-first
 * (big-endian) order.  All functions are wire-compatible with the original.
 */

#ifndef DORAEMON_PACK_H
#define DORAEMON_PACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Unpack: byte array → integer ---- */

uint64_t dp_u8_2_u64_lsb(uint8_t *dat);
uint64_t dp_u8_2_u64_msb(uint8_t *dat);
int64_t  dp_u8_2_i64_lsb(uint8_t *dat);
int64_t  dp_u8_2_i64_msb(uint8_t *dat);

uint32_t dp_u8_2_u32_lsb(uint8_t *dat);
uint32_t dp_u8_2_u32_msb(uint8_t *dat);
int32_t  dp_u8_2_i32_lsb(uint8_t *dat);
int32_t  dp_u8_2_i32_msb(uint8_t *dat);

uint16_t dp_u8_2_u16_lsb(uint8_t *dat);
uint16_t dp_u8_2_u16_msb(uint8_t *dat);
int16_t  dp_u8_2_i16_lsb(uint8_t *dat);
int16_t  dp_u8_2_i16_msb(uint8_t *dat);

/* ---- Helper macros (matching original doraemon_pack.h style) ---- */

/** Extract high byte of a uint16 */
#define DP_UINT16_H(x)  ((uint8_t)(((x) >> 8) & 0xff))
/** Extract low byte of a uint16 */
#define DP_UINT16_L(x)  ((uint8_t)((x) & 0xff))

/** Extract lower 32 bits of a 64-bit value (for printk formatting) */
#define DP_GET_U32(x)   ((uint32_t)((x) & 0xffffffff))

#ifdef __cplusplus
}
#endif

#endif /* DORAEMON_PACK_H */
