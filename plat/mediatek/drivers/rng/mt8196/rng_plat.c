/*
 * Copyright (c) 2025, MediaTek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <lib/utils_def.h>
#include <plat/common/platform.h>
#include <plat/common/plat_trng.h>
#include <stdbool.h>
#include <stdint.h>
#include "rng_plat.h"
#include "tzgp_lib_ssr_rng_export.h"

bool plat_get_entropy(uint64_t *out)
{
	uint32_t rng_val = 0;
	uint32_t ret = 0;

	assert(out);
	assert(!check_uptr_overflow((uintptr_t)out, sizeof(*out)));

	*out = ULL(0);

	/* Initialize CNTFRQ register which will be used by TRNG */
	if (read_cntfrq_el0() != plat_get_syscnt_freq2()) {
		write_cntfrq_el0(plat_get_syscnt_freq2());
	}

	ret = SSR_RNG_EnClk(SSR_TOP_BASE, true);
	if (ret) {
		ERROR("Failure to enable RNG clk, ret=0x%x\n", ret);
		return false;
	}

	ret = SSR_RNG_RstSw(SSR_RNG_BASE);
	if (ret) {
		ERROR("SSR reset SW failed\n");
		return false;
	}

	for (uint64_t i = 0; i < GET_RNG_ROUND; i++) {
		ret = SSR_RNG_GetVal(SSR_RNG_BASE, &rng_val);

		if (ret) {
			ERROR("Failure to get SSR TRNG, ret=0x%x\n", ret);
			*out = ULL(0);

			return false;
		}

		uint64_t tmp = (*out << BITS_NUM_32(i))
			    & GENMASK(BITS_NUM_32(i + 1) - 1, BITS_NUM_32(i));

		if (tmp < *out) {
			ERROR("%s: left shift operator overflow\n", __func__);
			panic();
		}

		*out = rng_val | tmp;
	}

	ret = SSR_RNG_EnClk(SSR_TOP_BASE, false);
	if (ret) {
		ERROR("Failure to disable RNG clk, ret=0x%x\n", ret);
		return false;
	}

	return true;
}
