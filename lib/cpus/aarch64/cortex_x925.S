/*
 * Copyright (c) 2023-2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <asm_macros.S>
#include <common/bl_common.h>
#include <cortex_x925.h>
#include <cpu_macros.S>
#include <wa_cve_2025_0647_cpprctx.h>
#include <plat_macros.S>

/* Hardware handled coherency */
#if HW_ASSISTED_COHERENCY == 0
#error "Cortex-X925 must be compiled with HW_ASSISTED_COHERENCY enabled"
#endif

/* 64-bit only core */
#if CTX_INCLUDE_AARCH32_REGS == 1
#error "Cortex-X925 supports only AArch64. Compile with CTX_INCLUDE_AARCH32_REGS=0"
#endif

workaround_runtime_start cortex_x925, ERRATUM(3324334), ERRATA_X925_3324334
	speculation_barrier
workaround_runtime_end cortex_x925, ERRATUM(3324334)

check_erratum_ls cortex_x925, ERRATUM(3324334), CPU_REV(0, 1)

workaround_reset_start cortex_x925, ERRATUM(3692980), ERRATA_X925_3692980
	sysreg_bit_set CORTEX_X925_CPUACTLR6_EL1, BIT(41)
workaround_reset_end cortex_x925, ERRATUM(3692980)

check_erratum_ls cortex_x925, ERRATUM(3692980), CPU_REV(0, 1)

.global check_erratum_cortex_x925_3701747

add_erratum_entry cortex_x925, ERRATUM(3701747), ERRATA_X925_3701747, NO_APPLY_AT_RESET

check_erratum_ls cortex_x925, ERRATUM(3701747), CPU_REV(0, 1)

workaround_reset_start cortex_x925, ERRATUM(3730893), ERRATA_X925_3730893
	sysreg_bitfield_insert CORTEX_X925_CPUACTLR_EL1, CORTEX_X925_CPUACTLR_EL1_LOAD_BIT, \
	CORTEX_X925_CPUACTLR_EL1_LOAD_SHIFT, CORTEX_X925_CPUACTLR_EL1_LOAD_WIDTH
workaround_reset_end cortex_x925, ERRATUM(3730893)

check_erratum_ls cortex_x925, ERRATUM(3730893), CPU_REV(0, 1)

workaround_reset_start cortex_x925, ERRATUM(3865185), ERRATA_X925_3865185
	sysreg_bit_set CORTEX_X925_CPUACTLR2_EL1, BIT(22)
workaround_reset_end cortex_x925, ERRATUM(3865185)

check_erratum_ls cortex_x925, ERRATUM(3865185), CPU_REV(0, 1)

/* Disable hardware page aggregation. Enables mitigation for `CVE-2024-5660` */
workaround_reset_start cortex_x925, CVE(2024, 5660), WORKAROUND_CVE_2024_5660
	sysreg_bit_set CORTEX_X925_CPUECTLR_EL1, BIT(46)
workaround_reset_end cortex_x925, CVE(2024, 5660)

check_erratum_ls cortex_x925, CVE(2024, 5660), CPU_REV(0, 1)

workaround_reset_start cortex_x925, CVE(2024, 7881), WORKAROUND_CVE_2024_7881
	/* ---------------------------------
         * Sets BIT41 of CPUACTLR6_EL1 which
         * disables L1 Data cache prefetcher
         * ---------------------------------
         */
	sysreg_bit_set CORTEX_X925_CPUACTLR6_EL1, BIT(41)
workaround_reset_end cortex_x925, CVE(2024, 7881)

check_erratum_chosen cortex_x925, CVE(2024, 7881), WORKAROUND_CVE_2024_7881

	/*
	 * Instruction patch sequence to trap 'cpp rctx' instructions to EL3.
	 * Enables mitigation for CVE-2025-0647.
	 */
workaround_reset_start cortex_x925, CVE(2025, 647), WORKAROUND_CVE_2025_0647
	mov	x0, #WA_PATCH_SLOT(3)
	bl	wa_cve_2025_0647_instruction_patch
workaround_reset_end cortex_x925, CVE(2025, 647)

check_erratum_chosen cortex_x925, CVE(2025, 647), WORKAROUND_CVE_2025_0647

#if WORKAROUND_CVE_2025_0647
func cortex_x925_impl_defined_el3_handler
	mov	x0, #0

	/* See if this call came from trap handler. */
	cmp	x1, #EC_IMP_DEF_EL3
	bne	wa_cve_2025_0647_do_cpp_wa
	orr	x0, x0, #WA_IS_TRAP_HANDLER
	b	wa_cve_2025_0647_do_cpp_wa
endfunc cortex_x925_impl_defined_el3_handler
#endif

cpu_reset_func_start cortex_x925
	/* Disable speculative loads */
	msr	SSBS, xzr
	apply_erratum cortex_x925, ERRATUM(3324334), ERRATA_X925_3324334
cpu_reset_func_end cortex_x925

	/* ----------------------------------------------------
	 * HW will do the cache maintenance while powering down
	 * ----------------------------------------------------
	 */
func cortex_x925_core_pwr_dwn
	/* ---------------------------------------------------
	 * Enable CPU power down bit in power control register
	 * ---------------------------------------------------
	 */
	sysreg_bit_set CORTEX_X925_CPUPWRCTLR_EL1, CORTEX_X925_CPUPWRCTLR_EL1_CORE_PWRDN_BIT
	isb
	ret
endfunc cortex_x925_core_pwr_dwn

	/* ---------------------------------------------
	 * This function provides Cortex-X925 specific
	 * register information for crash reporting.
	 * It needs to return with x6 pointing to
	 * a list of register names in ascii and
	 * x8 - x15 having values of registers to be
	 * reported.
	 * ---------------------------------------------
	 */
.section .rodata.cortex_x925_regs, "aS"
cortex_x925_regs:  /* The ascii list of register names to be reported */
	.asciz	"cpuectlr_el1", ""

func cortex_x925_cpu_reg_dump
	adr	x6, cortex_x925_regs
	mrs	x8, CORTEX_X925_CPUECTLR_EL1
	ret
endfunc cortex_x925_cpu_reg_dump

#if WORKAROUND_CVE_2025_0647
declare_cpu_ops_wa_4_eh cortex_x925, CORTEX_X925_MIDR, \
	cortex_x925_reset_func, \
	CPU_NO_EXTRA1_FUNC, \
	CPU_NO_EXTRA2_FUNC, \
	CPU_NO_EXTRA3_FUNC, \
	check_erratum_cortex_x925_7881, \
	cortex_x925_impl_defined_el3_handler, \
	cortex_x925_core_pwr_dwn
#else
declare_cpu_ops_wa_4 cortex_x925, CORTEX_X925_MIDR, \
	cortex_x925_reset_func, \
	CPU_NO_EXTRA1_FUNC, \
	CPU_NO_EXTRA2_FUNC, \
	CPU_NO_EXTRA3_FUNC, \
	check_erratum_cortex_x925_7881, \
	cortex_x925_core_pwr_dwn
#endif

