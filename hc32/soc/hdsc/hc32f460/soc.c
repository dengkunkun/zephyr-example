/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SoC early init for HC32F460
 *
 * Uses the DDL's LL_PERIPH_WE/WP API to unlock peripheral write-protect
 * registers. The underlying functions (GPIO_REG_Unlock, PWC_FCG0_REG_Unlock,
 * EFM_REG_Unlock, PWC_REG_Unlock) are simple register writes that don't
 * depend on CMSIS Core, so they work fine with Zephyr's CMSIS 6.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <hc32_ll.h>

/*
 * HC32F460 DDL requires an assertion handler.
 * Redirect to Zephyr's __ASSERT_NO_MSG.
 */
void DDL_AssertHandler(void)
{
	__ASSERT_NO_MSG(0);
}

void soc_early_init_hook(void)
{
	/* SystemInit is called by the DDL startup code, but since Zephyr
	 * provides its own startup (crt0), we call it explicitly here.
	 * SystemInit sets VTOR, configures FPU access, and updates
	 * SystemCoreClock.
	 */
	SystemInit();

	/*
	 * Unlock all peripheral write-protect registers.
	 *
	 * LL_PERIPH_WE calls the DDL unlock functions for each peripheral group:
	 *   LL_PERIPH_GPIO       → GPIO_REG_Unlock()    (PWPR = 0xA501)
	 *   LL_PERIPH_FCG        → PWC_FCG0_REG_Unlock() (FCG0PC = 0xA5010001)
	 *   LL_PERIPH_PWC_CLK_RMU→ PWC_REG_Unlock()     (FPRC |= 0xA501/0xA502)
	 *   LL_PERIPH_EFM        → EFM_REG_Unlock()     (FAPRT = 0x123, 0x3210)
	 *   LL_PERIPH_SRAM       → SRAM_REG_Unlock()    (SRAM WTPR)
	 */
	LL_PERIPH_WE(LL_PERIPH_ALL);
}
