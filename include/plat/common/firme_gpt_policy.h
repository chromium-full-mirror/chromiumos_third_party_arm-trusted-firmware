/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GPT_POLICY_H
#define FIRME_GPT_POLICY_H

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct firme_gpt_manager_range {
	uint64_t base;
	uint64_t top;
	bool host_managed;
};

/*
 * Immutable platform assignment of protected address ranges. Exception
 * ranges are half-open, PAR-aligned, ordered, and non-overlapping.
 */
struct firme_gpt_policy {
	bool default_host_managed;
	const struct firme_gpt_manager_range *exceptions;
	size_t exception_count;
};

const struct firme_gpt_policy *plat_firme_get_gpt_policy(void);

/*
 * Return an existing Root PAS mapping for [physical_addr, physical_addr + size),
 * or -ENOENT when the range is not statically mapped. The mapping must cover
 * the complete range and remain valid for the lifetime of EL3 firmware.
 */
int plat_firme_get_static_root_mapping(uint64_t physical_addr, size_t size,
				       uintptr_t *virtual_addr);

#endif /* __ASSEMBLER__ */
#endif /* FIRME_GPT_POLICY_H */
