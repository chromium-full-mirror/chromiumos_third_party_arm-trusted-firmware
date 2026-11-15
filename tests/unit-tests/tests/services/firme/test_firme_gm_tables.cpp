/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <CppUTest/TestHarness.h>

extern "C" {
#include <cdefs.h>
#include <errno.h>
#include <lib/utils_def.h>
#include <stdint.h>
#include <string.h>

#include "firme_gm_geometry.h"
#include "firme_gm_tables.h"
}

/* Literal expected encodings are independent of the production GPT macros. */
static const uint8_t base_gpis[] = { 0, 8, 9, 10, 11, 15 };
static const uint8_t reserved_gpis[] = { 1, 2, 3, 6, 7, 12, 14 };
static const uint64_t donor = 0x80000000ULL;
static uint64_t l0[3];
static uint64_t storage[16384 + 16] __aligned(64);
static uint64_t *const table = storage + 8;
static uint64_t expected_l0[3];
static uint64_t expected_storage[16384 + 16];
static size_t table_size;
static uint64_t par_size;
static int address_error;
static int donor_error;
static bool gdi;
static bool gpc2;

enum event_kind { CLEAN, FLUSH, STORE_BARRIER, TLBI, COMPLETE };

struct maintenance_event {
	event_kind kind;
	uintptr_t address;
	size_t size;
};

static maintenance_event events[8];
static unsigned int event_count;

static void check_contents(void)
{
	MEMCMP_EQUAL(expected_l0, l0, sizeof(l0));

	/* Includes both canaries and unused storage for the smaller-table case. */
	MEMCMP_EQUAL(expected_storage, storage, sizeof(storage));
}

static void snapshot(void)
{
	memcpy(expected_l0, l0, sizeof(l0));
	memcpy(expected_storage, storage, sizeof(storage));
	event_count = 0;
}

static void record(event_kind kind, uintptr_t address = 0, size_t size = 0)
{
	check_contents();
	CHECK(event_count < ARRAY_SIZE(events));
	events[event_count++] = { kind, address, size };
}

extern "C" {
/* Symbols referenced by the platform's common declarations. */
char __RO_START__[1];
char __RO_END__[1];
char __RW_END__[1];

bool is_feat_rme_gdi_supported(void)
{
	return gdi;
}

bool is_feat_rme_gpc2_supported(void)
{
	return gpc2;
}

uint64_t firme_gm_get_l1_entry_count(void)
{
	return table_size / 8;
}

size_t firme_gm_get_l1_table_size(void)
{
	return table_size;
}

uint64_t firme_gm_get_par_size(void)
{
	return par_size;
}

int firme_gm_get_l0_entry_address(uint64_t target_addr, uintptr_t *address)
{
	UNSIGNED_LONGLONGS_EQUAL(par_size, target_addr);
	if (address_error != 0) {
		return address_error;
	}

	*address = (uintptr_t)&l0[1];
	return 0;
}

int firme_gm_validate_l1_object(uint64_t donor_addr)
{
	CHECK(donor_addr == donor || donor_addr == donor + table_size ||
	      donor_addr == ((1ULL << 48) | donor));
	return donor_error;
}

void clean_dcache_range(uintptr_t address, size_t size)
{
	record(CLEAN, address, size);
}

void flush_dcache_range(uintptr_t address, size_t size)
{
	record(FLUSH, address, size);
}

void dsboshst(void)
{
	record(STORE_BARRIER);
}

void dsbosh(void)
{
	record(COMPLETE);
}

void test_tlbirpaos(uintptr_t address, unsigned long size)
{
	record(TLBI, address, size);
}
}

static uint64_t granules(uint8_t gpi)
{
	return 0x1111111111111111ULL * gpi;
}

static void fill(uint64_t descriptor)
{
	for (size_t i = 0; i < table_size / 8; i++) {
		table[i] = descriptor;
	}
}

static void check_event(unsigned int index, event_kind kind,
			uintptr_t address = 0, size_t size = 0)
{
	CHECK(index < event_count);
	LONGS_EQUAL(kind, events[index].kind);
	UNSIGNED_LONGLONGS_EQUAL(address, events[index].address);
	UNSIGNED_LONGLONGS_EQUAL(size, events[index].size);
}

static void check_read_only(void)
{
	check_contents();
	UNSIGNED_LONGS_EQUAL(0, event_count);
}

static void check_validation(int result, uint8_t expected_gpi = 0)
{
	uint8_t output = 0x5a;

	snapshot();
	LONGS_EQUAL(result,
		firme_gm_tables_validate((uintptr_t)table, &output));
	UNSIGNED_LONGS_EQUAL(result == 0 ? expected_gpi : 0x5a, output);
	check_read_only();
}

static void check_publication(unsigned int size_encoding)
{
	check_contents();
	UNSIGNED_LONGS_EQUAL(4, event_count);
	check_event(0, FLUSH, (uintptr_t)&l0[1], 8);
	check_event(1, STORE_BARRIER);
	check_event(2, TLBI, par_size, size_encoding);
	check_event(3, COMPLETE);
}

TEST_GROUP(firme_gm_tables) {
	TEST_SETUP()
	{
		table_size = 128 * 1024;
		par_size = 1ULL << 30;
		address_error = 0;
		donor_error = 0;
		gdi = false;
		gpc2 = false;

		l0[0] = 0x1122334455667788ULL;
		l0[1] = 0x91;
		l0[2] = 0x8877665544332211ULL;
		memset(storage, 0x6d, sizeof(storage));
		snapshot();
	}
};

TEST(firme_gm_tables, decodes_base_block_gpis_without_writes)
{
	for (uint8_t value : base_gpis) {
		l0[1] = 1 | (value << 4);
		uint8_t output = 0x5a;

		snapshot();
		LONGS_EQUAL(0, firme_gm_tables_get_block(par_size, &output));
		UNSIGNED_LONGS_EQUAL(value, output);
		check_read_only();
	}
}

TEST(firme_gm_tables, rejects_wrong_kind_and_malformed_l0_blocks)
{
	const uint64_t malformed[] = {
		0, donor | 3, 0x92, 0x191, (1ULL << 63) | 0x91,
	};

	for (uint64_t descriptor : malformed) {
		l0[1] = descriptor;
		uint8_t output = 0x5a;

		snapshot();
		LONGS_EQUAL(-EINVAL,
			firme_gm_tables_get_block(par_size, &output));
		UNSIGNED_LONGS_EQUAL(0x5a, output);
		check_read_only();
	}

	for (uint8_t value : reserved_gpis) {
		l0[1] = 1 | (value << 4);
		uint8_t output = 0x5a;

		snapshot();
		LONGS_EQUAL(-EINVAL,
			firme_gm_tables_get_block(par_size, &output));
		UNSIGNED_LONGS_EQUAL(0x5a, output);
		check_read_only();
	}
}

TEST(firme_gm_tables, decodes_high_donor_without_dereferencing_it)
{
	const uint64_t pa = (1ULL << 48) | donor;
	uint64_t output = 0x5a;

	l0[1] = pa | 3;
	snapshot();
	LONGS_EQUAL(0, firme_gm_tables_get_donor(par_size, &output));
	UNSIGNED_LONGLONGS_EQUAL(pa, output);
	check_read_only();
}

TEST(firme_gm_tables, leaves_discovered_donor_geometry_to_lifecycle)
{
	uint64_t output = 0x5a;

	/* Encodable address, but not aligned to the complete L1 object. */
	l0[1] = (donor + 4096) | 3;
	donor_error = -EINVAL;
	snapshot();
	LONGS_EQUAL(0, firme_gm_tables_get_donor(par_size, &output));
	UNSIGNED_LONGLONGS_EQUAL(donor + 4096, output);
	check_read_only();
}

TEST(firme_gm_tables, rejects_wrong_kind_and_malformed_l0_tables)
{
	const uint64_t malformed[] = {
		0, 0x91, donor | 2, donor | 0x13,
		donor | 0x803, (1ULL << 52) | donor | 3,
	};

	for (uint64_t descriptor : malformed) {
		uint64_t output = 0x5a;

		l0[1] = descriptor;
		snapshot();
		LONGS_EQUAL(-EINVAL,
			firme_gm_tables_get_donor(par_size, &output));
		UNSIGNED_LONGLONGS_EQUAL(0x5a, output);
		check_read_only();
	}
}

TEST(firme_gm_tables, rejects_null_inspection_outputs)
{
	LONGS_EQUAL(-EINVAL, firme_gm_tables_get_block(par_size, NULL));
	LONGS_EQUAL(-EINVAL, firme_gm_tables_get_donor(par_size, NULL));
	check_read_only();
}

TEST(firme_gm_tables, propagates_l0_address_failure_before_access)
{
	uint8_t gpi = 0x5a;
	uint64_t pa = 0x5a;

	address_error = -ERANGE;
	LONGS_EQUAL(-ERANGE, firme_gm_tables_get_block(par_size, &gpi));
	LONGS_EQUAL(-ERANGE, firme_gm_tables_get_donor(par_size, &pa));
	LONGS_EQUAL(-ERANGE, firme_gm_tables_attach(par_size, donor, 9));
	LONGS_EQUAL(-ERANGE, firme_gm_tables_detach(par_size, donor, 9));
	UNSIGNED_LONGS_EQUAL(0x5a, gpi);
	UNSIGNED_LONGLONGS_EQUAL(0x5a, pa);
	check_read_only();
}

TEST(firme_gm_tables, feature_gpis_require_their_own_capability)
{
	const uint8_t values[] = { 4, 5, 13 };

	for (uint8_t value : values) {
		for (unsigned int flags = 0; flags < 4; flags++) {
			gdi = (flags & 1) != 0;
			gpc2 = (flags & 2) != 0;
			bool supported = value == 13 ? gpc2 : gdi;
			uint8_t output = 0x5a;

			l0[1] = 1 | (value << 4);
			fill(granules(value));
			snapshot();
			LONGS_EQUAL(supported ? 0 : -EINVAL,
				firme_gm_tables_get_block(par_size, &output));
			UNSIGNED_LONGS_EQUAL(supported ? value : 0x5a, output);
			check_read_only();
			check_validation(supported ? 0 : -EINVAL, value);

			fill(0x101 | (value << 4));
			check_validation(supported ? 0 : -EINVAL, value);
		}
	}
}

TEST(firme_gm_tables, initializes_complete_images_before_cache_maintenance)
{
	const uint8_t values[] = { 0, 4, 5, 8, 9, 10, 11, 13, 15 };
	const size_t sizes[] = { 128 * 1024, 32 * 1024 };

	gdi = true;
	gpc2 = true;

	for (size_t size : sizes) {
		table_size = size;

		for (uint8_t value : values) {
			memset(storage, 0x6d, sizeof(storage));
			snapshot();
			memset(expected_storage + 8, value * 17, size);
			firme_gm_tables_init((uintptr_t)table, value);
			check_contents();
			UNSIGNED_LONGS_EQUAL(2, event_count);
			check_event(0, CLEAN, (uintptr_t)table, size);
			check_event(1, COMPLETE);
		}
	}
}

TEST(firme_gm_tables, accepts_uniform_base_granules)
{
	for (uint8_t value : base_gpis) {
		fill(granules(value));
		check_validation(0, value);
	}
}

TEST(firme_gm_tables, rejects_each_differing_nibble_at_table_boundaries)
{
	const size_t indices[] = { 0, 8192, 16383 };

	for (size_t index : indices) {
		for (unsigned int shift = 0; shift < 64; shift += 4) {
			fill(0x9999999999999999ULL);
			table[index] ^= 1ULL << shift; /* One NS becomes Secure. */
			check_validation(-EINVAL);
		}
	}
}

TEST(firme_gm_tables, rejects_reserved_gpis_including_the_final_nibble)
{
	for (uint8_t value : reserved_gpis) {
		fill(granules(value));
		check_validation(-EINVAL);

		fill(0x9999999999999999ULL);
		table[16383] = 0x0999999999999999ULL |
				((uint64_t)value << 60);
		check_validation(-EINVAL);
	}
}

TEST(firme_gm_tables, accepts_all_contiguous_sizes_and_base_gpis)
{
	for (uint8_t value : base_gpis) {
		for (uint64_t contig = 1; contig <= 3; contig++) {
			fill(1 | (value << 4) | (contig << 8));
			check_validation(0, value);
		}
	}
}

TEST(firme_gm_tables, accepts_mixed_forms_inside_one_contiguous_region)
{
	fill(0x9999999999999999ULL);

	/* Even overlapping sizes may coexist when effective GPIs agree. */
	table[1] = 0x391;
	table[17] = 0x291;
	table[16383] = 0x191;
	check_validation(0, 9);

	/* Also exercise a contiguous first word selecting the common GPI. */
	table[0] = 0x391;
	check_validation(0, 9);
}

TEST(firme_gm_tables, rejects_malformed_contiguous_words_and_mixed_gpis)
{
	const uint64_t malformed[] = {
		0x91, /* Reserved Contig encoding. */
		0x591, /* RES0 bit 10. */
		(1ULL << 63) | 0x191,
		0x161, /* Reserved GPI. */
		0x181, /* Valid Secure, but the rest of the table is NS. */
	};

	for (uint64_t descriptor : malformed) {
		fill(0x391);
		table[16383] = descriptor;
		check_validation(-EINVAL);
	}

	/* A contiguous head must not cause the remainder to be skipped. */
	fill(0x391);
	table[8191] = 0x9999999999999998ULL;
	check_validation(-EINVAL);
}

TEST(firme_gm_tables, rejects_invalid_validation_arguments)
{
	uint8_t output = 0x5a;

	LONGS_EQUAL(-EINVAL, firme_gm_tables_validate(0, &output));
	LONGS_EQUAL(-EINVAL,
		firme_gm_tables_validate((uintptr_t)table + 1, &output));
	LONGS_EQUAL(-EINVAL,
		firme_gm_tables_validate((uintptr_t)table, NULL));

	table_size = 0;
	LONGS_EQUAL(-EINVAL,
		firme_gm_tables_validate((uintptr_t)table, &output));
	UNSIGNED_LONGS_EQUAL(0x5a, output);
	check_read_only();
}

TEST(firme_gm_tables, attaches_and_detaches_with_all_par_invalidation_sizes)
{
	const unsigned int par_bits[] = { 30, 34, 36, 39 };
	const unsigned int encodings[] = { 6, 7, 8, 9 };
	const uint64_t pa = (1ULL << 48) | donor;

	for (unsigned int i = 0; i < 4; i++) {
		par_size = 1ULL << par_bits[i];
		l0[1] = 0x91;
		snapshot();
		expected_l0[1] = pa | 3;
		LONGS_EQUAL(0, firme_gm_tables_attach(par_size, pa, 9));
		check_publication(encodings[i]);

		snapshot();
		expected_l0[1] = 0x81;
		LONGS_EQUAL(0, firme_gm_tables_detach(par_size, pa, 8));
		check_publication(encodings[i]);
	}
}

TEST(firme_gm_tables, publishes_each_supported_block_gpi)
{
	const uint8_t values[] = { 0, 4, 5, 8, 9, 10, 11, 13, 15 };

	gdi = true;
	gpc2 = true;

	for (uint8_t value : values) {
		l0[1] = 1 | (value << 4);
		snapshot();
		expected_l0[1] = donor | 3;
		LONGS_EQUAL(0, firme_gm_tables_attach(par_size, donor, value));
		check_publication(6);

		snapshot();
		expected_l0[1] = 1 | (value << 4);
		LONGS_EQUAL(0, firme_gm_tables_detach(par_size, donor, value));
		check_publication(6);
	}
}

TEST(firme_gm_tables, rejects_stale_or_malformed_attach_state)
{
	const uint64_t states[] = { 0x81, donor | 3, 0, 0x191 };

	for (uint64_t state : states) {
		l0[1] = state;
		snapshot();
		LONGS_EQUAL(-ESTALE,
			firme_gm_tables_attach(par_size, donor, 9));
		check_read_only();
	}
}

TEST(firme_gm_tables, rejects_stale_or_malformed_detach_state)
{
	const uint64_t states[] = {
		(donor + table_size) | 3, 0x91, 0, donor | 0x13,
	};

	for (uint64_t state : states) {
		l0[1] = state;
		snapshot();
		LONGS_EQUAL(-ESTALE,
			firme_gm_tables_detach(par_size, donor, 9));
		check_read_only();
	}
}

TEST(firme_gm_tables, rejects_invalid_publication_gpis_before_mutation)
{
	const uint8_t values[] = { 1, 4, 5, 13, 16, 255 };

	for (uint8_t value : values) {
		LONGS_EQUAL(-EINVAL,
			firme_gm_tables_attach(par_size, donor, value));
		LONGS_EQUAL(-EINVAL,
			firme_gm_tables_detach(par_size, donor, value));
		check_read_only();
	}
}

TEST(firme_gm_tables, rejects_invalid_donor_geometry_before_publication)
{
	donor_error = -EINVAL;
	LONGS_EQUAL(-EINVAL, firme_gm_tables_attach(par_size, donor, 9));

	l0[1] = donor | 3;
	snapshot();
	LONGS_EQUAL(-EINVAL, firme_gm_tables_detach(par_size, donor, 9));
	check_read_only();
}

TEST(firme_gm_tables, rejects_unsupported_par_size_before_publication)
{
	par_size = 1ULL << 31;
	LONGS_EQUAL(-EINVAL, firme_gm_tables_attach(par_size, donor, 9));
	check_read_only();

	l0[1] = donor | 3;
	snapshot();
	LONGS_EQUAL(-EINVAL, firme_gm_tables_detach(par_size, donor, 9));
	check_read_only();
}

TEST(firme_gm_tables, scrubs_exact_mapped_extent_before_cache_completion)
{
	const size_t sizes[] = { 128 * 1024, 32 * 1024 };

	for (size_t size : sizes) {
		table_size = size;
		memset(storage, 0x6d, sizeof(storage));
		snapshot();
		memset(expected_storage + 8, 0, size);
		firme_gm_tables_scrub((uintptr_t)table);
		check_contents();
		UNSIGNED_LONGS_EQUAL(2, event_count);
		check_event(0, FLUSH, (uintptr_t)table, size);
		check_event(1, COMPLETE);
	}
}
