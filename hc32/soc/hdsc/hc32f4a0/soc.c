/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SoC early init for HC32F4A0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <hc32_ll.h>

void DDL_AssertHandler(void)
{
	__ASSERT_NO_MSG(0);
}

void soc_early_init_hook(void)
{
	SystemInit();
	LL_PERIPH_WE(LL_PERIPH_ALL);
}
