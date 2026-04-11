/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SoC early init for HC32F460
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>

/* Write-protect unlock keys */
#define GPIO_PWPR_UNLOCK  0xA501U
#define PWC_FCG0PC_UNLOCK 0xA5010001UL
#define EFM_FAPRT_KEY1    0x00000123UL
#define EFM_FAPRT_KEY2    0x00003210UL

/* PWC FPRC register (Function Protect Register Control) */
#define PWC_FPRC_ADDR     0x40048090UL
#define PWC_FPRC_UNLOCK1  0xA501U  /* Unlock bits 0,1 */
#define PWC_FPRC_UNLOCK2  0xA502U  /* Unlock bit 1 */

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
	 * Unlock peripheral write-protect registers using direct register
	 * writes rather than DDL functions to avoid CMSIS version conflicts.
	 */

	/* Unlock PWC FPRC to allow writes to PWC/CLK/RMU registers */
	sys_write16(PWC_FPRC_UNLOCK1, PWC_FPRC_ADDR);

	/* Unlock FCG0 register */
	sys_write32(PWC_FCG0PC_UNLOCK, 0x40048010UL); /* PWC_FCG0PC */

	/* Unlock GPIO registers (PSPCR, PCCR, PINAER, PCRxy, PFSRxy) */
	sys_write16(GPIO_PWPR_UNLOCK, 0x40053BFCUL); /* GPIO_PWPR @ offset 0x3FC */

	/* Unlock EFM (flash) registers */
	sys_write32(EFM_FAPRT_KEY1, 0x40010404UL); /* EFM_FAPRT */
	sys_write32(EFM_FAPRT_KEY2, 0x40010404UL);
}
