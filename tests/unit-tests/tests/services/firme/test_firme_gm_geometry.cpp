/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <CppUTest/TestHarness.h>

extern "C" {
#include <errno.h>
#include <stdint.h>

#include <lib/extensions/rme.h>
#include <plat/common/common_def.h>
#include <services/firme/firme_granule_mgmt.h>

#include "firme_gm_geometry.h"

/* Linker symbols referenced by the platform's common declarations. */
char __RO_START__[1];
char __RO_END__[1];
char __RW_END__[1];

static uint64_t gpccr;
static uint64_t gptbr;
static unsigned int register_reads;

uint64_t read_gpccr_el3(void)
{
	register_reads++;
	return gpccr;
}

uint64_t read_gptbr_el3(void)
{
	register_reads++;
	return gptbr;
}
}

struct geometry_case {
	unsigned int pps;
	unsigned int pgs;
	unsigned int l0gptsz;
	uint64_t pps_size;
	uint64_t par_size;
	size_t granule_size;
	size_t table_size;
	uint64_t donor_count;
};

static const geometry_case cases[] = {
	{ 0, 0, 0, 1ULL << 32, SZ_1G, SZ_4K, SZ_128K, 32 },
	{ 0, 2, 0, 1ULL << 32, SZ_1G, SZ_16K, SZ_32K, 2 },
	{ 1, 1, 4, 1ULL << 36, 1ULL << 34, SZ_64K, SZ_128K, 2 },
	{ 6, 1, 9, 1ULL << 52, 1ULL << 39, SZ_64K, SZ_4M, 64 },
};

static void configure(unsigned int pps, unsigned int pgs,
	       unsigned int l0gptsz, uintptr_t l0_base = 0x10000)
{
	gpccr = ((uint64_t)pps << GPCCR_PPS_SHIFT) |
		((uint64_t)pgs << GPCCR_PGS_SHIFT) |
		((uint64_t)l0gptsz << GPCCR_L0GPTSZ_SHIFT);
	gptbr = (l0_base >> GPTBR_BADDR_VAL_SHIFT) << GPTBR_BADDR_SHIFT;
}

static void initialize(const geometry_case config)
{
	configure(config.pps, config.pgs, config.l0gptsz);
	LONGS_EQUAL(0, firme_gm_geometry_init());
	UNSIGNED_LONGLONGS_EQUAL(config.pps_size, firme_gm_get_pps_size());
	UNSIGNED_LONGLONGS_EQUAL(config.par_size, firme_gm_get_par_size());
	UNSIGNED_LONGLONGS_EQUAL(config.granule_size,
				firme_gm_get_granule_size());
	UNSIGNED_LONGLONGS_EQUAL(config.table_size,
				firme_gm_get_l1_table_size());
	UNSIGNED_LONGLONGS_EQUAL(config.table_size / 8,
				firme_gm_get_l1_entry_count());
	UNSIGNED_LONGLONGS_EQUAL(config.donor_count,
				firme_gm_get_l1_granule_count());
}

TEST_GROUP(firme_gm_geometry) {
	TEST_SETUP()
	{
		register_reads = 0;
		configure(0, 0, 0);
	}

};

TEST(firme_gm_geometry, explicit_sizes_and_counts)
{
	for (const auto config : cases) {
		initialize(config);
	}
}

TEST(firme_gm_geometry, supported_register_encodings)
{
	const unsigned int pps_bits[] = { 32, 36, 40, 42, 44, 48, 52 };
	const unsigned int pgs_bits[] = { 12, 16, 14 };
	const unsigned int l0gptsz_values[] = { 0, 4, 6, 9 };

	for (unsigned int pps = 0; pps < ARRAY_SIZE(pps_bits); pps++) {
		for (unsigned int pgs = 0; pgs < ARRAY_SIZE(pgs_bits); pgs++) {
			for (unsigned int l0gptsz : l0gptsz_values) {
				configure(pps, pgs, l0gptsz);
				LONGS_EQUAL(0, firme_gm_geometry_init());
				UNSIGNED_LONGLONGS_EQUAL(1ULL << pps_bits[pps],
							firme_gm_get_pps_size());
				UNSIGNED_LONGLONGS_EQUAL(1ULL << pgs_bits[pgs],
							firme_gm_get_granule_size());
				UNSIGNED_LONGLONGS_EQUAL(1ULL << (30 + l0gptsz),
							firme_gm_get_par_size());
				UNSIGNED_LONGLONGS_EQUAL(
					(pps << FIRME_GM_PPS_SHIFT) |
					(l0gptsz << FIRME_GM_L0GPTSZ_SHIFT) |
					(pgs << FIRME_GM_PGS_SHIFT),
					firme_gm_geometry_feature_register());
			}
		}
	}
}

TEST(firme_gm_geometry, rejects_reserved_encodings_and_zero_l0)
{
	configure(7, 0, 0);
	LONGS_EQUAL(-EINVAL, firme_gm_geometry_init());
	configure(0, 3, 0);
	LONGS_EQUAL(-EINVAL, firme_gm_geometry_init());
	for (unsigned int l0gptsz = 0; l0gptsz <= 15; l0gptsz++) {
		if ((l0gptsz == 0) || (l0gptsz == 4) ||
		    (l0gptsz == 6) || (l0gptsz == 9)) {
			continue;
		}
		configure(0, 0, l0gptsz);
		LONGS_EQUAL(-EINVAL, firme_gm_geometry_init());
	}
	configure(0, 0, 0, 0);
	LONGS_EQUAL(-EINVAL, firme_gm_geometry_init());
}

TEST(firme_gm_geometry, caches_registers_and_preserves_config_on_init_failure)
{
	initialize(cases[2]);
	unsigned int reads = register_reads;
	uint64_t features = firme_gm_geometry_feature_register();

	configure(0, 0, 0);
	UNSIGNED_LONGLONGS_EQUAL(cases[2].pps_size, firme_gm_get_pps_size());
	UNSIGNED_LONGLONGS_EQUAL(cases[2].par_size, firme_gm_get_par_size());
	UNSIGNED_LONGLONGS_EQUAL(cases[2].granule_size,
				firme_gm_get_granule_size());
	UNSIGNED_LONGLONGS_EQUAL(features, firme_gm_geometry_feature_register());
	LONGS_EQUAL(0, firme_gm_validate_par(cases[2].par_size));
	LONGS_EQUAL(0, firme_gm_validate_l1_object(cases[2].table_size));
	UNSIGNED_LONGS_EQUAL(reads, register_reads);
	configure(0, 0, 0, 0);
	LONGS_EQUAL(-EINVAL, firme_gm_geometry_init());
	UNSIGNED_LONGLONGS_EQUAL(features, firme_gm_geometry_feature_register());
	UNSIGNED_LONGLONGS_EQUAL(cases[2].table_size,
				firme_gm_get_l1_table_size());
}

TEST(firme_gm_geometry, validates_target_boundaries)
{
	for (const auto config : cases) {
		initialize(config);
		uint64_t pps_size = config.pps_size;
		uint64_t par_size = config.par_size;
		uint64_t invalid[] = {
			par_size + 1, /* Byte offset breaks PAR alignment. */
			par_size + config.granule_size, /* Granule aligned only. */
			pps_size, /* Starts at the exclusive PPS limit. */
			pps_size + par_size, /* Starts beyond PPS. */
			UINT64_MAX & ~(par_size - 1), /* End address overflows. */
			pps_size - par_size + 1, /* Misaligned; ends past PPS. */
		};

		for (uint64_t target_addr : invalid) {
			LONGS_EQUAL(-EINVAL, firme_gm_validate_par(target_addr));
		}
		LONGS_EQUAL(0, firme_gm_validate_par(0));
		LONGS_EQUAL(0, firme_gm_validate_par(pps_size - par_size));
	}
}

TEST(firme_gm_geometry, validates_donor_boundaries)
{
	for (const auto config : cases) {
		initialize(config);
		uint64_t pps_size = config.pps_size;
		uint64_t table_size = config.table_size;
		uint64_t invalid[] = {
			0, /* Null donor address. */
			table_size + 1, /* Byte offset breaks table alignment. */
			table_size + config.granule_size, /* Granule aligned only. */
			pps_size, /* Starts at the exclusive PPS limit. */
			pps_size + table_size, /* Starts beyond PPS. */
			UINT64_MAX & ~(table_size - 1), /* End address overflows. */
			pps_size - table_size + 1, /* Misaligned; ends past PPS. */
		};

		for (uint64_t donor_addr : invalid) {
			LONGS_EQUAL(-EINVAL,
				firme_gm_validate_l1_object(donor_addr));
		}
		LONGS_EQUAL(0, firme_gm_validate_l1_object(table_size));
		LONGS_EQUAL(0,
			firme_gm_validate_l1_object(pps_size - table_size));
	}
}

TEST(firme_gm_geometry, rejects_non_donatable_geometry)
{
	configure(0, 1, 0);
	LONGS_EQUAL(0, firme_gm_geometry_init());
	UNSIGNED_LONGLONGS_EQUAL(SZ_8K, firme_gm_get_l1_table_size());
	UNSIGNED_LONGLONGS_EQUAL(0, firme_gm_get_l1_granule_count());
	LONGS_EQUAL(-EINVAL, firme_gm_validate_l1_object(SZ_64K));
	/* A complete PAR must fit inside PPS. */
	configure(0, 0, 9);
	LONGS_EQUAL(0, firme_gm_geometry_init());
	LONGS_EQUAL(-EINVAL, firme_gm_validate_par(0));
}

TEST(firme_gm_geometry, computes_l0_addresses_without_accessing_descriptors)
{
	for (const auto config : cases) {
		initialize(config);
		uintptr_t address = 0;
		uint64_t targets[] = { 0, config.par_size,
				       config.pps_size - config.par_size };

		/* Synthetic L0 address is deliberately not mapped by this test. */
		for (uint64_t target_addr : targets) {
			LONGS_EQUAL(0, firme_gm_get_l0_entry_address(
				target_addr, &address));
			UNSIGNED_LONGLONGS_EQUAL(
				0x10000 + (target_addr / config.par_size) * 8,
				address);
		}
		LONGS_EQUAL(-EINVAL, firme_gm_get_l0_entry_address(0, NULL));
		address = 0x1234;
		LONGS_EQUAL(-EINVAL, firme_gm_get_l0_entry_address(1, &address));
		UNSIGNED_LONGLONGS_EQUAL(0x1234, address);
		LONGS_EQUAL(-EINVAL, firme_gm_get_l0_entry_address(
			config.pps_size, &address));
		UNSIGNED_LONGLONGS_EQUAL(0x1234, address);
	}
}

TEST(firme_gm_geometry, decodes_high_l0_base)
{
	uintptr_t address = 0;
	uintptr_t l0_base = (1ULL << 48) | 0x10000;

	configure(6, 1, 9, l0_base);
	LONGS_EQUAL(0, firme_gm_geometry_init());
	LONGS_EQUAL(0, firme_gm_get_l0_entry_address(0, &address));
	UNSIGNED_LONGLONGS_EQUAL(l0_base, address);
}

TEST(firme_gm_geometry, computes_l1_index_and_gpi_shift)
{
	const size_t entries_per_2mb[] = { 32, 8, 2, 2 };

	for (unsigned int i = 0; i < ARRAY_SIZE(cases); i++) {
		const auto config = cases[i];

		initialize(config);
		uint64_t granule_size = config.granule_size;
		uint64_t par_size = config.par_size;
		uint64_t addresses[] = {
			0, granule_size, 15 * granule_size, 16 * granule_size,
			par_size - granule_size, par_size,
		};
		uint64_t indices[] = { 0, 0, 0, 1, config.table_size / 8 - 1, 0 };
		unsigned int shifts[] = { 0, 4, 60, 0, 60, 0 };

		for (unsigned int j = 0; j < ARRAY_SIZE(addresses); j++) {
			UNSIGNED_LONGLONGS_EQUAL(indices[j],
				firme_gm_get_l1_entry_index(addresses[j]));
			UNSIGNED_LONGS_EQUAL(shifts[j],
				firme_gm_get_l1_gpi_shift(addresses[j]));
		}
		UNSIGNED_LONGS_EQUAL(entries_per_2mb[i],
				    firme_gm_get_l1_entries_per_2mb());
	}
}
