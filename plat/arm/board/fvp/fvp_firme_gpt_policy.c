/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <lib/utils_def.h>
#include <plat/arm/common/arm_def.h>
#include <plat/common/common_def.h>
#include <plat/common/firme_gpt_policy.h>
#include <platform_def.h>

/*
 * Otherwise unlisted FVP PARs are Host-managed. Keep platform, firmware,
 * trusted DRAM, and the complete configured PCIe aperture under EL3 control.
 */
static const struct firme_gpt_manager_range fvp_firme_gpt_exceptions[] = {
	{
		.base = 0U,
		.top = ULL(0x100000000),
		.host_managed = false,
	},
	{
		.base = PLAT_ARM_PCI_MEM_2_BASE,
		.top = PLAT_ARM_PCI_MEM_2_BASE + PLAT_ARM_PCI_MEM_2_SIZE,
		.host_managed = false,
	},
};

static const struct firme_gpt_policy fvp_firme_gpt_policy = {
	.default_host_managed = true,
	.exceptions = fvp_firme_gpt_exceptions,
	.exception_count = ARRAY_SIZE(fvp_firme_gpt_exceptions),
};

const struct firme_gpt_policy *plat_firme_get_gpt_policy(void)
{
	return &fvp_firme_gpt_policy;
}

int plat_firme_get_static_root_mapping(uint64_t physical_addr, size_t size,
				       uintptr_t *virtual_addr)
{
	uint64_t end;

	if ((virtual_addr == NULL) || (size == 0U) ||
	    __builtin_add_overflow(physical_addr, (uint64_t)size, &end)) {
		return -EINVAL;
	}

	if ((physical_addr >= ARM_L1_GPT_BASE) &&
	    (end <= (ARM_L1_GPT_BASE + ARM_L1_GPT_SIZE))) {
		*virtual_addr = (uintptr_t)physical_addr;
		return 0;
	}

	return -ENOENT;
}
