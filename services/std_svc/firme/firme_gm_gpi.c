/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "firme_gm_geometry.h"
#include "firme_gm_tables.h"
#include "firme_gm_private.h"

#include <arch.h>
#include <arch_features.h>
#include <arch_helpers.h>
#include <common/debug.h>
#include <lib/extensions/rme.h>
#include <lib/gpt_rme/gpt_rme.h>
#include <lib/smccc.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <plat/common/common_def.h>
#include <services/firme/firme_granule_mgmt.h>

/* Get the descriptor or NSE bits from GPI encoding. */
#define GPI_TO_DESC(_gpi)	(firme_gpi_config[_gpi].desc)
#define GPI_TO_NSE(_gpi)	\
	(((uint64_t)firme_gpi_config[_gpi].nse << GPT_NSE_SHIFT) | \
	 ((uint64_t)firme_gpi_config[_gpi].nse2 << GPT_NSE2_SHIFT))

/* Get the index into the L1 table from a physical address */
#define GPT_L1_INDEX(_pa)	firme_gm_get_l1_entry_index(_pa)

/* Number of 128-bit L1 entries in 2MB, 32MB and 512MB */
#define L1_QWORDS_2MB	(firme_gm_get_l1_entries_per_2mb() / 2U)
#define L1_QWORDS_32MB	(L1_QWORDS_2MB * 16U)
#define L1_QWORDS_512MB	(L1_QWORDS_32MB * 16U)

/*
 * Lookup table used to speed up granule transitions by associating GPIs and
 * relevant information such as descriptors, NSE fields, and transition
 * policies defined by FIRME rules.
 */
typedef struct {
	uint64_t desc;
	uint8_t nse;
	uint8_t nse2;
	uint16_t policy[3];
} firme_gpi_config_t;

/*
 * Internal structure to retrieve the values from get_gpi_params();
 * todo: extend this to gpi_geometry
 */
typedef struct {
	uint64_t gpt_l1_desc;
	uint64_t *gpt_l1_addr;
	unsigned int idx;
	unsigned int gpi_shift;
	unsigned int gpi;
} firme_gpi_info_t;

typedef void (*gpt_shatter_func)(uintptr_t base,
				 const firme_gpi_info_t *gpi_info,
				 uint64_t l1_desc);
typedef void (*gpt_tlbi_func)(uintptr_t base);

/*
 * Look-up structure for
 * invalidating TLBs of GPT entries by Physical address, last level.
 */
typedef struct {
	gpt_tlbi_func function;
	size_t mask;
} gpt_tlbi_lookup_t;

/*
 * This table allows us to easily look up GPI-specific information such as
 * descriptors, nse/nse2 fields, and allowed transitions without using large
 * blocks of nested conditionals which both take up a lot of space and are
 * very slow.
 *
 * This array contains an entry for each valid GPI, unused values are all zeros.
 * GPIs such as SA, NSP, and NSO are not part of base FEAT_RME so those policy
 * flags are set at runtime when FEAT_RME_GDI and FEAT_RME_GPC2 are enabled.
 *
 * uint64_t desc       The L1 descriptor associated with a GPI
 * uint8_t nse         NSE bits
 * uint8_t nse2        NSE2 bits
 * uint16_t policy[3] Contains bit fields representing which GPIs a given
 *                     security state can transition this GPI to. 0=s, 1=ns, and
 *                     0x2=realm.
 */
static firme_gpi_config_t firme_gpi_config[] = {
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ GPT_L1_SA_DESC, 0, GPT_NSE2_SA, { 0x0, 0x0, 0x0 } },
	{ GPT_L1_NSP_DESC, 0, GPT_NSE2_NSP, { 0x0, 0x0, 0x0 } },
	{ 0 },
	{ 0 },
	{ GPT_L1_SECURE_DESC,
	  GPT_NSE_SECURE,
	  0,
	  { (1 << GPT_GPI_NS), 0x0, 0x0 } },
	{ GPT_L1_NS_DESC,
	  GPT_NSE_NS,
	  0,
	  { (1 << GPT_GPI_SECURE), 0x0, (1 << GPT_GPI_REALM) } },
	{ GPT_L1_ROOT_DESC, GPT_NSE_ROOT, 0, { 0x0, 0x0, 0x0 } },
	{ GPT_L1_REALM_DESC, GPT_NSE_REALM, 0, { 0x0, 0x0, (1 << GPT_GPI_NS) } },
	{ 0 },
	{ GPT_L1_NSO_DESC, GPT_NSE_NS, 0, { 0x0, 0x0, 0x0 } },
	{ 0 },
	{ 0 },
};

static void fill_desc(uint64_t *l1, uint64_t l1_desc, unsigned int cnt)
{
	uint128_t *l1_quad = (uint128_t *)l1;
	uint128_t l1_quad_desc = (uint128_t)l1_desc | ((uint128_t)l1_desc << 64);

	VERBOSE("GPT: %s(%p 0x%"PRIx64" %u)\n", __func__, l1, l1_desc, cnt);

	for (unsigned int i = 0U; i < cnt; i++) {
		*l1_quad++ = l1_quad_desc;
	}
}

/*
 * Helper function to check if all L1 entries in 2MB block have
 * the same Granules descriptor value.
 *
 * Parameters
 *   base		Base address of the region to be checked
 *   gpi_info		Pointer to 'gpt_config_t' structure
 *   l1_desc		GPT Granules descriptor with all entries
 *			set to the same GPI.
 *
 * Return
 *   true if L1 all entries have the same descriptor value, false otherwise.
 */
__unused static bool check_fuse_2mb(uint64_t base,
				    const firme_gpi_info_t *gpi_info,
				    uint64_t l1_desc)
{
	/* Number of L1 entries in 2MB block */
	unsigned int cnt = firme_gm_get_l1_entries_per_2mb();

	/* Last L1 entry index in 2MB block */
	unsigned long idx = GPT_L1_INDEX(ALIGN_2MB(base)) + cnt - 1UL;

	/*
	 * Start check from the last L1 entry and continue until the first
	 * non-matching to the passed Granules descriptor value is found.
	 */
	while (cnt-- != 0U) {
		if (gpi_info->gpt_l1_addr[idx--] != l1_desc) {
			/* Non-matching L1 entry found */
			return false;
		}
	}

	return true;
}

__unused static void fuse_2mb(uint64_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	/* L1 entry index of the start of 2MB block */
	unsigned long idx_2 = GPT_L1_INDEX(ALIGN_2MB(base));

	/* 2MB Contiguous descriptor */
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 2MB);

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n", __func__, base, l1_desc);

	fill_desc(&gpi_info->gpt_l1_addr[idx_2], l1_cont_desc, L1_QWORDS_2MB);
}

/*
 * Helper function to check if all 1st L1 entries of 2MB blocks
 * in 32MB have the same 2MB Contiguous descriptor value.
 *
 * Parameters
 *   base		Base address of the region to be checked
 *   gpi_info		Pointer to 'gpt_config_t' structure
 *   l1_desc		GPT Granules descriptor.
 *
 * Return
 *   true if all L1 entries have the same descriptor value, false otherwise.
 */
__unused static bool check_fuse_32mb(uint64_t base, const firme_gpi_info_t *gpi_info,
					uint64_t l1_desc)
{
	const size_t l1_entries_2mb = firme_gm_get_l1_entries_per_2mb();

	/* The 1st L1 entry index of the last 2MB block in 32MB */
	unsigned long idx = GPT_L1_INDEX(ALIGN_32MB(base)) +
			    (15UL * l1_entries_2mb);

	/* 2MB Contiguous descriptor */
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 2MB);

	/* Number of 2MB blocks in 32MB */
	unsigned int cnt = 16U;

	/* Set the first L1 entry to 2MB Contiguous descriptor */
	gpi_info->gpt_l1_addr[GPT_L1_INDEX(ALIGN_2MB(base))] = l1_cont_desc;

	/*
	 * Start check from the 1st L1 entry of the last 2MB block and
	 * continue until the first non-matching to 2MB Contiguous descriptor
	 * value is found.
	 */
	while (cnt-- != 0U) {
		if (gpi_info->gpt_l1_addr[idx] != l1_cont_desc) {
			/* Non-matching L1 entry found */
			return false;
		}
		idx -= l1_entries_2mb;
	}

	return true;
}

__unused static void fuse_32mb(uint64_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	/* L1 entry index of the start of 32MB block */
	unsigned long idx_32 = GPT_L1_INDEX(ALIGN_32MB(base));

	/* 32MB Contiguous descriptor */
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 32MB);

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n", __func__, base, l1_desc);

	fill_desc(&gpi_info->gpt_l1_addr[idx_32], l1_cont_desc, L1_QWORDS_32MB);
}

/*
 * Helper function to check if all 1st L1 entries of 32MB blocks
 * in 512MB have the same 32MB Contiguous descriptor value.
 *
 * Parameters
 *   base		Base address of the region to be checked
 *   gpi_info		Pointer to 'gpt_config_t' structure
 *   l1_desc		GPT Granules descriptor.
 *
 * Return
 *   true if all L1 entries have the same descriptor value, false otherwise.
 */
__unused static bool check_fuse_512mb(uint64_t base, const firme_gpi_info_t *gpi_info,
					uint64_t l1_desc)
{
	const size_t l1_entries_32mb = firme_gm_get_l1_entries_per_2mb() * 16U;

	/* The 1st L1 entry index of the last 32MB block in 512MB */
	unsigned long idx = GPT_L1_INDEX(ALIGN_512MB(base)) +
			    (15UL * l1_entries_32mb);

	/* 32MB Contiguous descriptor */
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 32MB);

	/* Number of 32MB blocks in 512MB */
	unsigned int cnt = 16U;

	/* Set the first L1 entry to 2MB Contiguous descriptor */
	gpi_info->gpt_l1_addr[GPT_L1_INDEX(ALIGN_32MB(base))] = l1_cont_desc;

	/*
	 * Start check from the 1st L1 entry of the last 32MB block and
	 * continue until the first non-matching to 32MB Contiguous descriptor
	 * value is found.
	 */
	while (cnt-- != 0U) {
		if (gpi_info->gpt_l1_addr[idx] != l1_cont_desc) {
			/* Non-matching L1 entry found */
			return false;
		}
		idx -= l1_entries_32mb;
	}

	return true;
}

__unused static void fuse_512mb(uint64_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	/* L1 entry index of the start of 512MB block */
	unsigned long idx_512 = GPT_L1_INDEX(ALIGN_512MB(base));

	/* 512MB Contiguous descriptor */
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 512MB);

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n", __func__, base, l1_desc);

	fill_desc(&gpi_info->gpt_l1_addr[idx_512], l1_cont_desc, L1_QWORDS_512MB);
}

/*
 * Helper function to convert GPI entries in a single L1 table
 * from Granules to Contiguous descriptor.
 *
 * Parameters
 *   base		Base address of the region to be written
 *   gpi_info		Pointer to 'gpt_config_t' structure
 *   l1_desc		GPT Granules descriptor with all entries
 *			set to the same GPI.
 */
__unused static void fuse_block(uint64_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	/* Start with check for 2MB block */
	if (!check_fuse_2mb(base, gpi_info, l1_desc)) {
		/* Check for 2MB fusing failed */
		return;
	}

#if (RME_GPT_MAX_BLOCK == 2)
	fuse_2mb(base, gpi_info, l1_desc);
#else
	/* Check for 32MB block */
	if (!check_fuse_32mb(base, gpi_info, l1_desc)) {
		/* Check for 32MB fusing failed, fuse to 2MB */
		fuse_2mb(base, gpi_info, l1_desc);
		return;
	}

#if (RME_GPT_MAX_BLOCK == 32)
	fuse_32mb(base, gpi_info, l1_desc);
#else
	/* Check for 512MB block */
	if (!check_fuse_512mb(base, gpi_info, l1_desc)) {
		/* Check for 512MB fusing failed, fuse to 32MB */
		fuse_32mb(base, gpi_info, l1_desc);
		return;
	}

	/* Fuse to 512MB */
	fuse_512mb(base, gpi_info, l1_desc);

#endif	/* RME_GPT_MAX_BLOCK == 32 */
#endif	/* RME_GPT_MAX_BLOCK == 2 */
}

static void shatter_2mb(uintptr_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	unsigned long idx = GPT_L1_INDEX(ALIGN_2MB(base));

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n",
				__func__, base, l1_desc);

	/* Convert 2MB Contiguous block to Granules */
	fill_desc(&gpi_info->gpt_l1_addr[idx], l1_desc, L1_QWORDS_2MB);
}

static void shatter_32mb(uintptr_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	const size_t l1_entries_2mb = firme_gm_get_l1_entries_per_2mb();
	unsigned long idx = GPT_L1_INDEX(ALIGN_2MB(base));
	const uint64_t *l1_gran = &gpi_info->gpt_l1_addr[idx];
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 2MB);
	uint64_t *l1;

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n",
				__func__, base, l1_desc);

	/* Get index corresponding to 32MB aligned address */
	idx = GPT_L1_INDEX(ALIGN_32MB(base));
	l1 = &gpi_info->gpt_l1_addr[idx];

	/* 16 x 2MB blocks in 32MB */
	for (unsigned int i = 0U; i < 16U; i++) {
		/* Fill with Granules or Contiguous descriptors */
		fill_desc(l1, (l1 == l1_gran) ? l1_desc : l1_cont_desc,
			  l1_entries_2mb / 2U);
		l1 += l1_entries_2mb;
	}
}

static void shatter_512mb(uintptr_t base, const firme_gpi_info_t *gpi_info,
				uint64_t l1_desc)
{
	const size_t l1_entries_32mb = firme_gm_get_l1_entries_per_2mb() * 16U;
	unsigned long idx = GPT_L1_INDEX(ALIGN_32MB(base));
	const uint64_t *l1_32mb = &gpi_info->gpt_l1_addr[idx];
	uint64_t l1_cont_desc = GPT_L1_CONT_DESC(l1_desc, 32MB);
	uint64_t *l1;

	VERBOSE("GPT: %s(0x%"PRIxPTR" 0x%"PRIx64")\n",
				__func__, base, l1_desc);

	/* Get index corresponding to 512MB aligned address */
	idx = GPT_L1_INDEX(ALIGN_512MB(base));
	l1 = &gpi_info->gpt_l1_addr[idx];

	/* 16 x 32MB blocks in 512MB */
	for (unsigned int i = 0U; i < 16U; i++) {
		if (l1 == l1_32mb) {
			/* Shatter this 32MB block */
			shatter_32mb(base, gpi_info, l1_desc);
		} else {
			/* Fill 32MB with Contiguous descriptors */
			fill_desc(l1, l1_cont_desc, l1_entries_32mb / 2U);
		}

		l1 += l1_entries_32mb;
	}
}

/*
 * Helper function to convert GPI entries in a single L1 table
 * from Contiguous to Granules descriptor. This function updates
 * descriptor to Granules in passed 'gpt_config_t' structure as
 * the result of shuttering.
 *
 * Parameters
 *   base		Base address of the region to be written
 *   gpi_info		Pointer to 'gpt_config_t' structure
 *   l1_desc		GPT Granules descriptor set this range to.
 */
__unused static void shatter_block(uint64_t base, firme_gpi_info_t *gpi_info,
				   uint64_t l1_desc)
{
	/* Look-up table for 2MB, 32MB and 512MB locks shattering */
	static const gpt_shatter_func gpt_shatter_lookup[] = {
		shatter_2mb,
		shatter_32mb,
		shatter_512mb
	};

	/* Look-up table for invalidation TLBs for 2MB, 32MB and 512MB blocks */
	static const gpt_tlbi_lookup_t tlbi_lookup[] = {
		{ tlbirpalos_2m, ~(SZ_2M - 1UL) },
		{ tlbirpalos_32m, ~(SZ_32M - 1UL) },
		{ tlbirpalos_512m, ~(SZ_512M - 1UL) }
	};

	/* Get shattering level from Contig field of Contiguous descriptor */
	unsigned long level = GPT_L1_CONT_CONTIG(gpi_info->gpt_l1_desc) - 1UL;

	/* Shatter contiguous block */
	gpt_shatter_lookup[level](base, gpi_info, l1_desc);

	tlbi_lookup[level].function(base & tlbi_lookup[level].mask);
	dsbosh();

	/*
	 * Update 'gpt_config_t' structure's descriptor to Granules to reflect
	 * the shattered GPI back to caller.
	 */
	gpi_info->gpt_l1_desc = l1_desc;
}

static void read_gpi(firme_gpi_info_t *gpi_info)
{
	gpi_info->gpt_l1_desc = (gpi_info->gpt_l1_addr)[gpi_info->idx];

	if ((gpi_info->gpt_l1_desc & GPT_L1_TYPE_CONT_DESC_MASK) ==
				 GPT_L1_TYPE_CONT_DESC) {
		/* Read GPI from Contiguous descriptor */
		gpi_info->gpi = (unsigned int)GPT_L1_CONT_GPI(gpi_info->gpt_l1_desc);
	} else {
		/* Read GPI from Granules descriptor */
		gpi_info->gpi = (unsigned int)((gpi_info->gpt_l1_desc >> gpi_info->gpi_shift) &
						GPT_L1_GRAN_DESC_GPI_MASK);
	}
}

/* Descriptor validity is independent of the caller's transition policy. */
static bool is_gpi_valid(unsigned int gpi)
{
	switch (gpi) {
	case GPT_GPI_NO_ACCESS:
	case GPT_GPI_SECURE:
	case GPT_GPI_NS:
	case GPT_GPI_ROOT:
#if ENABLE_RMM
	case GPT_GPI_REALM:
#endif
	case GPT_GPI_ANY:
		return true;
	case GPT_GPI_NSO:
		return is_feat_rme_gpc2_supported();
	case GPT_GPI_SA:
	case GPT_GPI_NSP:
		return is_feat_rme_gdi_supported();
	default:
		return false;
	}
}

/* Lifecycle callers reserve the target to stabilize its L0 association. */
static int read_l0(uint64_t target_addr, uint64_t *descriptor)
{
	uintptr_t address;
	int rc = firme_gm_get_l0_entry_address(target_addr, &address);

	if (rc != 0) {
		return rc;
	}

	*descriptor = __atomic_load_n((uint64_t *)address, __ATOMIC_ACQUIRE);
	return 0;
}

int firme_gm_tables_get_block(uint64_t target_addr, uint8_t *gpi)
{
	uint64_t descriptor;
	uint8_t block_gpi;
	int rc;

	if (gpi == NULL) {
		return -EINVAL;
	}

	rc = read_l0(target_addr, &descriptor);
	if (rc != 0) {
		return rc;
	}

	block_gpi = GPT_L0_BLKD_GPI(descriptor);
	if (!is_gpi_valid(block_gpi) ||
	    (descriptor != GPT_L0_BLK_DESC(block_gpi))) {
		return -EINVAL;
	}

	*gpi = block_gpi;
	return 0;
}

int firme_gm_tables_get_donor(uint64_t target_addr, uint64_t *donor_addr)
{
	uint64_t descriptor;
	uint64_t donor;
	int rc;

	if (donor_addr == NULL) {
		return -EINVAL;
	}

	rc = read_l0(target_addr, &descriptor);
	if (rc != 0) {
		return rc;
	}

	donor = (uintptr_t)GPT_L0_TBLD_ADDR(descriptor);
	if (descriptor != GPT_L0_TBL_DESC(donor)) {
		return -EINVAL;
	}

	/* Lifecycle validates donor geometry before mapping or accessing it. */
	*donor_addr = donor;
	return 0;
}

void firme_gm_tables_init(uintptr_t table_va, uint8_t gpi)
{
	uint64_t *table = (uint64_t *)table_va;
	uint64_t entry_count = firme_gm_get_l1_entry_count();
	uint64_t descriptor = GPT_BUILD_L1_DESC(gpi);

	assert((table_va != 0U) && is_aligned(table_va, sizeof(*table)));
	assert((entry_count != 0U) && is_gpi_valid(gpi));

	for (uint64_t entry_index = 0U; entry_index < entry_count; entry_index++) {
		table[entry_index] = descriptor;
	}

	clean_dcache_range(table_va, firme_gm_get_l1_table_size());
	dsbosh();
}

static int l1_gpi(uint64_t descriptor, uint8_t *gpi)
{
	uint8_t value;

	if ((descriptor & GPT_L1_TYPE_CONT_DESC_MASK) ==
	    GPT_L1_TYPE_CONT_DESC) {
		uint64_t contig = GPT_L1_CONT_CONTIG(descriptor);
		uint64_t expected;

		value = GPT_L1_CONT_GPI(descriptor);
		expected = GPT_L1_GPI_CONT_DESC(value,
			GPT_L1_TYPE_CONT_DESC |
			(contig << GPT_L1_CONT_DESC_CONTIG_SHIFT));
		if ((contig == 0U) || (descriptor != expected)) {
			return -EINVAL;
		}
	} else {
		value = GPT_L1_GRAN_GPI(descriptor);
		if (descriptor != GPT_BUILD_L1_DESC(value)) {
			return -EINVAL;
		}
	}

	if (!is_gpi_valid(value)) {
		return -EINVAL;
	}

	*gpi = value;
	return 0;
}

int firme_gm_tables_validate(uintptr_t table_va, uint8_t *gpi)
{
	const uint64_t *table = (const uint64_t *)table_va;
	uint64_t entry_count = firme_gm_get_l1_entry_count();
	uint8_t common_gpi = 0U;

	if ((gpi == NULL) || (table_va == 0U) ||
	    !is_aligned(table_va, sizeof(*table)) || (entry_count == 0U)) {
		return -EINVAL;
	}

	/*
	 * Mixed granule/contiguous forms are valid when all effective GPIs agree
	 * (Arm DDI 0615, RNNHCF). Inspect every word, including contiguous tails.
	 */
	for (uint64_t entry_index = 0U; entry_index < entry_count; entry_index++) {
		uint8_t entry_gpi;

		if (l1_gpi(table[entry_index], &entry_gpi) != 0) {
			return -EINVAL;
		}

		if (entry_index == 0U) {
			common_gpi = entry_gpi;
		} else if (entry_gpi != common_gpi) {
			return -EINVAL;
		}
	}

	*gpi = common_gpi;
	return 0;
}

static int publish_l0(uint64_t target_addr, uint64_t expected,
		      uint64_t replacement)
{
	uintptr_t address;
	u_register_t tlbi_size;
	uint64_t *descriptor;
	int rc = firme_gm_get_l0_entry_address(target_addr, &address);

	if (rc != 0) {
		return rc;
	}

	switch (firme_gm_get_par_size()) {
	case SZ_1G:
		tlbi_size = TLBI_SZ_1G;
		break;
	case (ULL(16) * SZ_1G):
		tlbi_size = TLBI_SZ_16G;
		break;
	case (ULL(64) * SZ_1G):
		tlbi_size = TLBI_SZ_64G;
		break;
	case (ULL(512) * SZ_1G):
		tlbi_size = TLBI_SZ_512G;
		break;
	default:
		return -EINVAL;
	}

	descriptor = (uint64_t *)address;
	if (__atomic_load_n(descriptor, __ATOMIC_ACQUIRE) != expected) {
		return -ESTALE;
	}

	__atomic_store_n(descriptor, replacement, __ATOMIC_RELEASE);
	flush_dcache_range(address, sizeof(*descriptor));
	dsboshst();
	TLBIRPAOS(target_addr, tlbi_size);
	dsbosh();
	return 0;
}

int firme_gm_tables_attach(uint64_t target_addr, uint64_t donor_addr,
			   uint8_t expected_gpi)
{
	if (!is_gpi_valid(expected_gpi) ||
	    (firme_gm_validate_l1_object(donor_addr) != 0)) {
		return -EINVAL;
	}

	return publish_l0(target_addr, GPT_L0_BLK_DESC(expected_gpi),
			  GPT_L0_TBL_DESC(donor_addr));
}

int firme_gm_tables_detach(uint64_t target_addr, uint64_t expected_donor,
			   uint8_t gpi)
{
	if (!is_gpi_valid(gpi) ||
	    (firme_gm_validate_l1_object(expected_donor) != 0)) {
		return -EINVAL;
	}

	return publish_l0(target_addr, GPT_L0_TBL_DESC(expected_donor),
			  GPT_L0_BLK_DESC(gpi));
}

void firme_gm_tables_scrub(uintptr_t table_va)
{
	size_t size = firme_gm_get_l1_table_size();

	assert((table_va != 0U) && is_aligned(table_va, sizeof(uint64_t)));
	assert(size != 0U);

	memset((void *)table_va, 0, size);
	flush_dcache_range(table_va, size);
	dsbosh();
}

static void tlbi_page_dsbosh(uintptr_t base)
{
	switch (firme_gm_get_granule_size()) {
	case SZ_4K:
		tlbirpalos_4k(base & ~(SZ_4K - 1UL));
		break;
	case SZ_16K:
		tlbirpalos_16k(base & ~(SZ_16K - 1UL));
		break;
	case SZ_64K:
		tlbirpalos_64k(base & ~(SZ_64K - 1UL));
		break;
	default:
		panic();
	}
	dsbosh();
}

/*
 * A helper to write the value (target_pas << gpi_shift) to the index of
 * the gpt_l1_addr.
 */
static inline void write_gpt(uint64_t *gpt_l1_desc, uint64_t *gpt_l1_addr,
			     unsigned int gpi_shift, unsigned int idx,
			     unsigned int target_pas)
{
	*gpt_l1_desc &= ~(GPT_L1_GRAN_DESC_GPI_MASK << gpi_shift);
	*gpt_l1_desc |= ((uint64_t)target_pas << gpi_shift);
	gpt_l1_addr[idx] = *gpt_l1_desc;

	dsboshst();
}

static inline bool is_gpi_transition_permitted(uint8_t caller,
					       uint8_t current_gpi,
					       uint8_t target_gpi)
{
	/*
	 * So we can use a small lookup table, change caller security state 0x21
	 * (from realm) to 0x2 so it can be an index.
	 */
	if (caller == SMC_FROM_REALM) {
		caller = 0x2;
	}

	assert(caller <= 0x2);
	assert(current_gpi <= GPT_GPI_ANY);

	return (firme_gpi_config[current_gpi].policy[caller] >> target_gpi) & 0x1;
}

static inline void gpt_write_entry(uint64_t base, uint8_t target_gpi,
				   firme_gpi_info_t *gpi_info)
{
	/* Update the GPI entry to the new state. */
	write_gpt(&gpi_info->gpt_l1_desc, gpi_info->gpt_l1_addr,
		  gpi_info->gpi_shift, gpi_info->idx, target_gpi);

	/* Ensure all agents observe new state. */
	tlbi_page_dsbosh(base);
}

static void flush_page_to_popa(uintptr_t addr)
{
	size_t size = firme_gm_get_granule_size();

	if (is_feat_mte2_supported()) {
		flush_dcache_to_popa_range_mte2(addr, size);
	} else {
		flush_dcache_to_popa_range(addr, size);
	}
}

static inline void gpt_delegate(uint64_t base, uint8_t target_gpi,
				firme_gpi_info_t *gpi_info)
{
	uint8_t source_gpi = gpi_info->gpi;

	/*
	 * In order to maintain mutual distrust between states, remove any data
	 * speculatively fetched into the target physical address space.
	 */
	flush_page_to_popa(base | GPI_TO_NSE(target_gpi));

	gpt_write_entry(base, target_gpi, gpi_info);

	/* Ensure scrubbed data has made it past PoPA */
	flush_page_to_popa(base | GPI_TO_NSE(source_gpi));
}

static inline void gpt_undelegate(uint64_t base, uint8_t target_gpi,
				  firme_gpi_info_t *gpi_info)
{
	uint8_t source_gpi = gpi_info->gpi;

	/*
	 * In order to maintain mutual distrust between states, remove access
	 * now, in order to guarantee that writes to the currently-accessible
	 * physical address space will not later become observable.
	 */
	write_gpt(&gpi_info->gpt_l1_desc, gpi_info->gpt_l1_addr,
		  gpi_info->gpi_shift, gpi_info->idx, GPT_GPI_NO_ACCESS);

	/* Ensure all agents observe NO ACCESS state. */
	tlbi_page_dsbosh(base);

	/*
	 * Ensure that the scrubbed data have made it past the PoPA for both
	 * old and new security states.
	 */
	flush_page_to_popa(base | GPI_TO_NSE(source_gpi));
	flush_page_to_popa(base | GPI_TO_NSE(target_gpi));

	gpt_write_entry(base, target_gpi, gpi_info);
}

/*
 * Helper to retrieve the gpt_l1_* information from the base address
 * returned in gpi_info.
 */
static int get_gpi_params(uint64_t base, firme_gpi_info_t *gpi_info)
{
	uint64_t gpt_l0_desc;
	int rc;
	__unused unsigned int block_idx;

	rc = read_l0(round_down(base, firme_gm_get_par_size()), &gpt_l0_desc);
	if (rc != 0) {
		return rc;
	}

	if (GPT_L0_TYPE(gpt_l0_desc) != GPT_L0_TYPE_TBL_DESC) {
		VERBOSE("GPT: Granule is not covered by a table descriptor!\n");
		VERBOSE("      Base=0x%"PRIx64"\n", base);
		return -EINVAL;
	}

	/* Get the table index and GPI shift from PA */
	gpi_info->gpt_l1_addr = GPT_L0_TBLD_ADDR(gpt_l0_desc);
	gpi_info->idx = (unsigned int)GPT_L1_INDEX(base);
	gpi_info->gpi_shift = firme_gm_get_l1_gpi_shift(base);

	return 0;
}

/*
 * This function is for firme handler to set GPI encoding to 'target_gpi'
 * based on current_gpi and src_sec_state.
 *
 * The transition of GPI from source to target are based on FIRME rules
 *
 * Parameters
 *   base               Base address of the first granule to transition, aligned
 *                      to granule size.
 *   target_gpi         GPI to transition the granules to.
 *   src_sec_state      Security state of the requesting entity. This will be
 *                      combined with target_gpi to determine whether a
 *                      transition is allowed.
 */
static int gm_gpi_set(uint64_t base, uint8_t target_gpi, uint8_t src_sec_state)
{
	firme_gpi_info_t gpi_info = { 0, NULL, 0, 0, 0 };
	int res;
	size_t size;

	/* Ensure that MMU and caches are enabled */
	assert((read_sctlr_el3() & SCTLR_C_BIT) != 0UL);

	/* Calculate total region size and zero out granule count. */
	size = firme_gm_get_granule_size();

	/* Make sure target GPI is valid. */
	if (!is_gpi_valid(target_gpi)) {
		VERBOSE("GPT: Invalid target GPI value in request: %u\n",
			target_gpi);
		return -EPERM;
	}

	/* Check that base and size are valid */
	if ((ULONG_MAX - base) < size) {
		VERBOSE("GPT: Transition request address overflow!\n");
		VERBOSE("      Base=0x%" PRIx64 "\n", base);
		VERBOSE("      Size=%lu\n", size);
		return -EINVAL;
	}

	/* Make sure base and size are valid */
	if (((base & (size - 1UL)) != 0UL) ||
	    ((base + size) >= firme_gm_get_pps_size())) {
		VERBOSE("GPT: Invalid granule transition address range!\n");
		VERBOSE("      Base=0x%" PRIx64 "\n", base);
		VERBOSE("      Size=%lu\n", size);
		return -EINVAL;
	}

	/* Get GPI info for next granule to transition. */
	res = get_gpi_params(base, &gpi_info);
	if (res != 0) {
		return res;
	}

	firme_gpt_lock(base);

	read_gpi(&gpi_info);

	/* Verify that transition of this granule is allowed. */
	if (!is_gpi_transition_permitted(src_sec_state, gpi_info.gpi,
					 target_gpi)) {
		VERBOSE("(%s) Sec state %u is not allowed to transition %u to %u!\n",
			__func__, src_sec_state, gpi_info.gpi, target_gpi);
		firme_gpt_unlock(base);
		return -EPERM;
	}

#if (RME_GPT_MAX_BLOCK != 0)
	/* Check for Contiguous descriptor */
	if ((gpi_info.gpt_l1_desc & GPT_L1_TYPE_CONT_DESC_MASK) ==
	    GPT_L1_TYPE_CONT_DESC) {
		shatter_block(base, &gpi_info, GPI_TO_DESC(gpi_info.gpi));
	}
#endif

	if (((target_gpi == GPT_GPI_NS) && (gpi_info.gpi == GPT_GPI_NSO)) ||
	    ((target_gpi == GPT_GPI_NSO) && (gpi_info.gpi == GPT_GPI_NS))) {
		/* Handle NS/NSO transition. */
		gpt_write_entry(base, target_gpi, &gpi_info);
	} else if ((target_gpi == GPT_GPI_NS) || (target_gpi == GPT_GPI_NSO)) {
		/* Handle undelegate transition. */
		gpt_undelegate(base, target_gpi, &gpi_info);
	} else {
		/* Handle delegate transition. */
		gpt_delegate(base, target_gpi, &gpi_info);
	}

#if (RME_GPT_MAX_BLOCK != 0)
	if (gpi_info.gpt_l1_desc == GPI_TO_DESC(target_gpi)) {
		/* Try to fuse */
		fuse_block(base, &gpi_info, GPI_TO_DESC(target_gpi));
	}
#endif

	firme_gpt_unlock(base);

	return 0;
}

uint32_t firme_gm_gpi_set(uint64_t base, uint64_t gcnt, uint64_t attrs,
			  uint64_t flags, uint64_t *gcnt_ret)
{
	uint32_t ret;
	uint8_t target_gpi;
	uint32_t src_sec_state = caller_sec_state(flags);

	assert(is_feat_rme_supported());

	if (gcnt == 0U) {
		return -EINVAL;
	}

	/* Extract target GPI value from attributes in x3. */
	target_gpi = (attrs >> FIRME_GM_GPI_SET_TGT_GPI_SHIFT) &
		FIRME_GM_GPI_SET_TGT_GPI_MASK;

	/* gpi set currently supports one granule  */
	ret = gm_gpi_set(base, target_gpi, src_sec_state);
	if (ret == 0) {
		*gcnt_ret = 1U;
	}

	return ret;
}

void firme_gm_gpi_init(void)
{
	assert(is_feat_rme_supported());

	/* Enable these GPIs in NSO/NSP transition policies used by GPI set */
	if (is_feat_rme_gdi_supported()) {
		firme_gpi_config[GPT_GPI_NS].policy[SMC_FROM_NON_SECURE] |=
			((1 << GPT_GPI_NSP) | (1 << GPT_GPI_SA));
		firme_gpi_config[GPT_GPI_NSO].policy[SMC_FROM_NON_SECURE] |=
			((1 << GPT_GPI_NSP) | (1 << GPT_GPI_SA));
		firme_gpi_config[GPT_GPI_NSP].policy[SMC_FROM_NON_SECURE] |=
			((1 << GPT_GPI_NS) | (1 << GPT_GPI_NSO));
		firme_gpi_config[GPT_GPI_SA].policy[SMC_FROM_NON_SECURE] |=
			((1 << GPT_GPI_NS) | (1 << GPT_GPI_NSO));
	}

	/* Enable NSO in NS transition policies used by FIRME GPI set. */
	if (is_feat_rme_gpc2_supported()) {
		firme_gpi_config[GPT_GPI_NS].policy[SMC_FROM_NON_SECURE] |=
			(1 << GPT_GPI_NSO);
		firme_gpi_config[GPT_GPI_NSO].policy[SMC_FROM_NON_SECURE] |=
			(1 << GPT_GPI_NS);
	}
}
