/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file doraemon_pack.c
 * @brief Byte-order conversion helpers — straight port of original doraemon_pack.c.
 */

#include "doraemon_pack.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(doraemon_pack, LOG_LEVEL_INF);

union u64u8_u {
	uint64_t n;
	uint8_t  a[8];
};

union u32u8_u {
	uint32_t n;
	uint8_t  a[4];
};

union u16u8_u {
	uint16_t n;
	uint8_t  a[2];
};

uint64_t dp_u8_2_u64_lsb(uint8_t *dat)
{
	union u64u8_u tmp;

	tmp.a[0] = dat[0]; tmp.a[1] = dat[1];
	tmp.a[2] = dat[2]; tmp.a[3] = dat[3];
	tmp.a[4] = dat[4]; tmp.a[5] = dat[5];
	tmp.a[6] = dat[6]; tmp.a[7] = dat[7];
	return tmp.n;
}

uint64_t dp_u8_2_u64_msb(uint8_t *dat)
{
	union u64u8_u tmp;

	tmp.a[0] = dat[7]; tmp.a[1] = dat[6];
	tmp.a[2] = dat[5]; tmp.a[3] = dat[4];
	tmp.a[4] = dat[3]; tmp.a[5] = dat[2];
	tmp.a[6] = dat[1]; tmp.a[7] = dat[0];
	return tmp.n;
}

int64_t dp_u8_2_i64_lsb(uint8_t *dat)
{
	uint64_t a = dp_u8_2_u64_lsb(dat);
	return *((int64_t *)(&a));
}

int64_t dp_u8_2_i64_msb(uint8_t *dat)
{
	uint64_t a = dp_u8_2_u64_msb(dat);
	return *((int64_t *)(&a));
}

uint32_t dp_u8_2_u32_lsb(uint8_t *dat)
{
	union u32u8_u tmp;

	tmp.a[0] = dat[0]; tmp.a[1] = dat[1];
	tmp.a[2] = dat[2]; tmp.a[3] = dat[3];
	return tmp.n;
}

uint32_t dp_u8_2_u32_msb(uint8_t *dat)
{
	union u32u8_u tmp;

	tmp.a[0] = dat[3]; tmp.a[1] = dat[2];
	tmp.a[2] = dat[1]; tmp.a[3] = dat[0];
	return tmp.n;
}

int32_t dp_u8_2_i32_lsb(uint8_t *dat)
{
	uint32_t a = dp_u8_2_u32_lsb(dat);
	return *((int32_t *)(&a));
}

int32_t dp_u8_2_i32_msb(uint8_t *dat)
{
	uint32_t a = dp_u8_2_u32_msb(dat);
	return *((int32_t *)(&a));
}

uint16_t dp_u8_2_u16_lsb(uint8_t *dat)
{
	union u16u8_u tmp;

	tmp.a[0] = dat[0]; tmp.a[1] = dat[1];
	return tmp.n;
}

uint16_t dp_u8_2_u16_msb(uint8_t *dat)
{
	union u16u8_u tmp;

	tmp.a[0] = dat[1]; tmp.a[1] = dat[0];
	return tmp.n;
}

int16_t dp_u8_2_i16_lsb(uint8_t *dat)
{
	uint16_t b = dp_u8_2_u16_lsb(dat);
	return *((int16_t *)(&b));
}

int16_t dp_u8_2_i16_msb(uint8_t *dat)
{
	uint16_t b = dp_u8_2_u16_msb(dat);
	return *((int16_t *)(&b));
}
