/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file crc16_modbus.h
 * @brief Modbus CRC16 (table-lookup) — straight port of original crc16_modbus.c.
 */

#ifndef CRC16_MODBUS_H
#define CRC16_MODBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute Modbus CRC16 over a byte array.
 *
 * Uses the standard Modbus polynomial 0x8005 (reflected).  Identical to the
 * original bare-metal implementation — wire-compatible with all RS485 slaves.
 *
 * @param p Pointer to data.
 * @param n Number of bytes.
 * @return CRC16 value (little-endian: low byte sent first on wire).
 */
unsigned short calc_modbus_crc16(const unsigned char *p, int n);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_MODBUS_H */
