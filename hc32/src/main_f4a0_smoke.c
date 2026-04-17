/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal smoke test for the HC32F4A0PGTB port: print a heartbeat over
 * USART3 (defined as the Zephyr console in the board DTS) every second.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>

int main(void)
{
	uint32_t n = 0;

	printk("\n*** HC32F4A0PGTB smoke test: Zephyr %s running ***\n",
	       KERNEL_VERSION_STRING);

	while (1) {
		printk("[%u] alive, cpu=%u Hz\n", n++,
		       (unsigned int)sys_clock_hw_cycles_per_sec());
		k_msleep(1000);
	}

	return 0;
}
