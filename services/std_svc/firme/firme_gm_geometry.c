/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <arch_helpers.h>
#include <lib/extensions/rme.h>
#include <lib/utils_def.h>
#include <plat/common/common_def.h>
#include <services/firme/firme_granule_mgmt.h>

#include "firme_gm_geometry.h"

struct firme_gm_geometry {
	uintptr_t l0_base;
	uint64_t pps_size;
	uint64_t par_size;
	size_t granule_size;
	size_t l1_table_size;
	uint64_t l1_entry_count;
	uint64_t l1_granule_count;
	uint64_t feature_register;
	uint8_t pgs_bits;
	uint8_t par_bits;
};

static struct firme_gm_geometry geometry;

int firme_gm_geometry_init(void)
{
	static const uint8_t pps_bits[] = { 32U, 36U, 40U, 42U,
					    44U, 48U, 52U };
	static const uint8_t pgs_bits[] = { 12U, 16U, 14U };
	struct firme_gm_geometry config = { 0 };
	uint64_t gpccr = read_gpccr_el3();
	uint64_t gptbr = read_gptbr_el3();
	unsigned int pps = (gpccr >> GPCCR_PPS_SHIFT) & GPCCR_PPS_MASK;
	unsigned int pgs = (gpccr >> GPCCR_PGS_SHIFT) & GPCCR_PGS_MASK;
	unsigned int l0gptsz = (gpccr >> GPCCR_L0GPTSZ_SHIFT) &
			      GPCCR_L0GPTSZ_MASK;

	if ((pps >= ARRAY_SIZE(pps_bits)) || (pgs >= ARRAY_SIZE(pgs_bits)) ||
	    ((l0gptsz != GPCCR_L0GPTSZ_30BITS) &&
	     (l0gptsz != GPCCR_L0GPTSZ_34BITS) &&
	     (l0gptsz != GPCCR_L0GPTSZ_36BITS) &&
	     (l0gptsz != GPCCR_L0GPTSZ_39BITS))) {
		return -EINVAL;
	}

	config.l0_base =
		(uintptr_t)((gptbr >> GPTBR_BADDR_SHIFT) & GPTBR_BADDR_MASK)
		<< GPTBR_BADDR_VAL_SHIFT;
	if (config.l0_base == 0U) {
		return -EINVAL;
	}

	config.pgs_bits = pgs_bits[pgs];
	config.par_bits = l0gptsz + 30U;
	config.pps_size = ULL(1) << pps_bits[pps];
	config.par_size = ULL(1) << config.par_bits;
	config.granule_size = UL(1) << config.pgs_bits;
	/* Each 64-bit L1 entry describes sixteen granules. */
	config.l1_entry_count = config.par_size >>
				GPT_L1_IDX_SHIFT(config.pgs_bits);
	config.l1_table_size = config.l1_entry_count * sizeof(uint64_t);
	config.l1_granule_count = config.l1_table_size / config.granule_size;
	config.feature_register = ((uint64_t)pps << FIRME_GM_PPS_SHIFT) |
				  ((uint64_t)l0gptsz << FIRME_GM_L0GPTSZ_SHIFT) |
				  ((uint64_t)pgs << FIRME_GM_PGS_SHIFT);

	/* Publish only a complete, validated configuration. */
	geometry = config;
	return 0;
}

uint64_t firme_gm_geometry_feature_register(void)
{
	return geometry.feature_register;
}

uint64_t firme_gm_get_pps_size(void)
{
	return geometry.pps_size;
}

uint64_t firme_gm_get_par_size(void)
{
	return geometry.par_size;
}

size_t firme_gm_get_granule_size(void)
{
	return geometry.granule_size;
}

size_t firme_gm_get_l1_table_size(void)
{
	return geometry.l1_table_size;
}

uint64_t firme_gm_get_l1_granule_count(void)
{
	return geometry.l1_granule_count;
}

uint64_t firme_gm_get_l1_entry_count(void)
{
	return geometry.l1_entry_count;
}

int firme_gm_validate_par(uint64_t target_addr)
{
	uint64_t end;
	uint64_t size = geometry.par_size;

	if ((size == 0U) || ((target_addr & (size - 1U)) != 0U) ||
	    __builtin_add_overflow(target_addr, size, &end) ||
	    (end > geometry.pps_size)) {
		return -EINVAL;
	}

	return 0;
}

int firme_gm_validate_l1_object(uint64_t donor_addr)
{
	uint64_t end;
	size_t size = geometry.l1_table_size;
	size_t granule_size = geometry.granule_size;

	if ((granule_size == 0U) || (size == 0U) ||
	    ((size % granule_size) != 0U) || (donor_addr == 0U) ||
	    ((donor_addr & (size - 1U)) != 0U) ||
	    __builtin_add_overflow(donor_addr, (uint64_t)size, &end) ||
	    (end > geometry.pps_size)) {
		return -EINVAL;
	}

	return 0;
}

int firme_gm_get_l0_entry_address(uint64_t target_addr, uintptr_t *address)
{
	uintptr_t offset;

	if ((address == NULL) || (firme_gm_validate_par(target_addr) != 0)) {
		return -EINVAL;
	}

	offset = (target_addr >> geometry.par_bits) * sizeof(uint64_t);
	*address = geometry.l0_base + offset;
	return 0;
}

uint64_t firme_gm_get_l1_entry_index(uint64_t pa)
{
	return (pa >> GPT_L1_IDX_SHIFT(geometry.pgs_bits)) &
	       (geometry.l1_entry_count - 1U);
}

unsigned int firme_gm_get_l1_gpi_shift(uint64_t pa)
{
	return GPT_L1_GPI_IDX(geometry.pgs_bits, pa) << 2;
}

size_t firme_gm_get_l1_entries_per_2mb(void)
{
	return SZ_2M >> GPT_L1_IDX_SHIFT(geometry.pgs_bits);
}
