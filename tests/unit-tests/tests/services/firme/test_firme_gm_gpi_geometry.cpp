/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <CppUTest/TestHarness.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <arch.h>
#include <arch_helpers.h>
#include <lib/extensions/rme.h>
#include <lib/smccc.h>
#include <lib/spinlock.h>

#include "firme_gm_geometry.h"
#include "firme_gm_private.h"
#include "firme_gm_mapping.h"
#include "firme_gm_reservation.h"
}

static uint64_t l0[8192] __aligned(65536);
static uint64_t l1[524288] __aligned(4194304);
static uint64_t gpccr;
static uint64_t gptbr;
static unsigned int register_reads;
static unsigned int locks;
static unsigned int unlocks;
static unsigned int tlbis;
static unsigned int flushes;
static bool mte;
static unsigned int mte_flushes;
static unsigned int active_mappings;
static size_t tlbi_size;
static size_t flush_size;
static uintptr_t tlbi_address;
static uintptr_t flush_addresses[2];

extern "C" {
/* Linker symbols referenced by the platform's common declarations. */
unsigned char __RO_START__;
unsigned char __RO_END__;
unsigned char __RW_END__;

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

uint64_t read_sctlr_el3(void)
{
	return SCTLR_C_BIT;
}

bool is_feat_rme_supported(void)
{
	return true;
}
bool is_feat_rme_gdi_supported(void)
{
	return false;
}
bool is_feat_rme_gpc2_supported(void)
{
	return false;
}
bool is_feat_mte2_supported(void)
{
	return mte;
}
void dsbosh(void) { }
void dsboshst(void) { }

void clean_dcache_range(uintptr_t address, size_t size)
{
	(void)address;
	(void)size;
	FAIL("A granule transition must not initialize a whole table");
}

void flush_dcache_range(uintptr_t address, size_t size)
{
	(void)address;
	(void)size;
	FAIL("A granule transition must not publish or scrub a whole table");
}

void test_tlbirpaos(uintptr_t address, unsigned long size)
{
	(void)address;
	(void)size;
	FAIL("A granule transition must not invalidate a whole PAR");
}

void firme_gpt_lock(uint64_t base)
{
	(void)base;
	UNSIGNED_LONGS_EQUAL(unlocks, locks);
	locks++;
}

void firme_gpt_unlock(uint64_t base)
{
	(void)base;
	UNSIGNED_LONGS_EQUAL(unlocks + 1, locks);
	unlocks++;
}

void tlbirpalos_4k(uintptr_t address)
{
	tlbis++;
	tlbi_address = address;
	tlbi_size = 4096;
}

void tlbirpalos_16k(uintptr_t address)
{
	tlbis++;
	tlbi_address = address;
	tlbi_size = 16384;
}

void tlbirpalos_64k(uintptr_t address)
{
	tlbis++;
	tlbi_address = address;
	tlbi_size = 65536;
}

void tlbirpalos_2m(uintptr_t address)
{
	(void)address;
}
void tlbirpalos_32m(uintptr_t address)
{
	(void)address;
}
void tlbirpalos_512m(uintptr_t address)
{
	(void)address;
}

void flush_dcache_to_popa_range(uintptr_t address, size_t size)
{
	if (flushes < ARRAY_SIZE(flush_addresses)) {
		flush_addresses[flushes] = address;
	}
	flushes++;
	flush_size = size;
}

void flush_dcache_to_popa_range_mte2(uintptr_t address, size_t size)
{
	mte_flushes++;
	flush_dcache_to_popa_range(address, size);
}

void spin_lock(spinlock_t *lock)
{
	UNSIGNED_LONGS_EQUAL(0, lock->lock);
	lock->lock = 1;
}

bool spin_trylock(spinlock_t *lock)
{
	spin_lock(lock);
	return true;
}

void spin_unlock(spinlock_t *lock)
{
	UNSIGNED_LONGS_EQUAL(1, lock->lock);
	lock->lock = 0;
}

int firme_gm_map_root(uint64_t address, size_t size,
		      struct firme_gm_mapping *mapping)
{
	UNSIGNED_LONGLONGS_EQUAL((uintptr_t)l1, address);
	mapping->va = (uintptr_t)l1;
	mapping->size = size;
	active_mappings++;
	return 0;
}

void firme_gm_unmap_root(struct firme_gm_mapping *mapping)
{
	CHECK(active_mappings > 0);
	active_mappings--;
	mapping->size = 0;
}

int console_flush(void)
{
	return 0;
}
void el3_panic(void)
{
	abort();
}
}

static void initialize(unsigned int pgs, unsigned int l0gptsz)
{
	gpccr = (6ULL << GPCCR_PPS_SHIFT) |
		((uint64_t)pgs << GPCCR_PGS_SHIFT) |
		((uint64_t)l0gptsz << GPCCR_L0GPTSZ_SHIFT);
	gptbr = ((uintptr_t)l0 >> GPTBR_BADDR_VAL_SHIFT) <<
		GPTBR_BADDR_SHIFT;
	LONGS_EQUAL(0, firme_gm_geometry_init());
	firme_gm_gpi_init();
	l0[1] = GPT_L0_TBL_DESC((uintptr_t)l1);
}

static void check_transition(uint64_t base, size_t granule_size,
		      uint64_t entry, unsigned int shift)
{
	l1[entry] = GPT_L1_NS_DESC;
	uint64_t completed = 0;
	uint64_t expected = (GPT_L1_NS_DESC & ~(0xFULL << shift)) |
			    ((uint64_t)GPT_GPI_SECURE << shift);

	UNSIGNED_LONGS_EQUAL(0, firme_gm_gpi_set(base, 1, GPT_GPI_SECURE,
		SMC_FROM_SECURE, &completed));
	UNSIGNED_LONGLONGS_EQUAL(1, completed);
	UNSIGNED_LONGLONGS_EQUAL(expected, l1[entry]);
	UNSIGNED_LONGS_EQUAL(1, locks);
	UNSIGNED_LONGS_EQUAL(1, unlocks);
	UNSIGNED_LONGS_EQUAL(1, tlbis);
	UNSIGNED_LONGS_EQUAL(granule_size, tlbi_size);
	UNSIGNED_LONGLONGS_EQUAL(base, tlbi_address);
	UNSIGNED_LONGS_EQUAL(2, flushes);
	UNSIGNED_LONGS_EQUAL(granule_size, flush_size);
	UNSIGNED_LONGLONGS_EQUAL(base, flush_addresses[0]);
	UNSIGNED_LONGLONGS_EQUAL(base | (2ULL << 62), flush_addresses[1]);
	UNSIGNED_LONGLONGS_EQUAL(0, l1[entry - 1]);
	UNSIGNED_LONGLONGS_EQUAL(0, l0[0]);
	UNSIGNED_LONGLONGS_EQUAL(0, l0[2]);
}

TEST_GROUP(firme_gm_gpi_geometry) {
	struct firme_gm_reservation *held_reservation;

	TEST_SETUP()
	{
		held_reservation = NULL;
		memset(l0, 0, sizeof(l0));
		memset(l1, 0, sizeof(l1));
		register_reads = 0;
		locks = 0;
		unlocks = 0;
		tlbis = 0;
		flushes = 0;
		mte = false;
		mte_flushes = 0;
		active_mappings = 0;
	}

	TEST_TEARDOWN()
	{
		firme_gm_release(&held_reservation);
		UNSIGNED_LONGS_EQUAL(locks, unlocks);
		UNSIGNED_LONGS_EQUAL(0, active_mappings);
	}
};

TEST(firme_gm_gpi_geometry, uses_cached_large_par_and_64k_granules)
{
	initialize(1, 9);
	/* Changes after initialization must not affect the transition. */
	gpccr = 0;
	gptbr = 0;
	check_transition((2ULL << 39) - 65536, 65536, 524287, 60);
	UNSIGNED_LONGS_EQUAL(2, register_reads);
}

TEST(firme_gm_gpi_geometry, selects_4k_maintenance)
{
	initialize(0, 0);
	check_transition((2ULL << 30) - 4096, 4096, 16383, 60);
}

TEST(firme_gm_gpi_geometry, selects_16k_maintenance)
{
	initialize(2, 0);
	check_transition((2ULL << 30) - 16384, 16384, 4095, 60);
}

TEST(firme_gm_gpi_geometry, rejects_invalid_base_before_descriptor_access)
{
	initialize(1, 9);
	uint64_t completed = 0;

	LONGS_EQUAL(-EINVAL,
		firme_gm_gpi_set((1ULL << 39) + 1, 1, GPT_GPI_SECURE,
				 SMC_FROM_SECURE, &completed));
	UNSIGNED_LONGS_EQUAL(0, locks);
	UNSIGNED_LONGS_EQUAL(0, tlbis);
	UNSIGNED_LONGS_EQUAL(0, flushes);
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
}

TEST(firme_gm_gpi_geometry, checks_full_source_range_without_mutation)
{
	initialize(0, 0);
	l1[0] = GPT_L1_ROOT_DESC;
	LONGS_EQUAL(0, firme_gm_gpi_check(1ULL << 30, 16, GPT_GPI_ROOT));
	l1[0] = 0x9aaaaaaaaaaaaaaaULL;
	LONGS_EQUAL(-EACCES, firme_gm_gpi_check(1ULL << 30, 16, GPT_GPI_ROOT));
	UNSIGNED_LONGLONGS_EQUAL(0x9aaaaaaaaaaaaaaaULL, l1[0]);
	UNSIGNED_LONGS_EQUAL(0, tlbis);
	UNSIGNED_LONGS_EQUAL(0, flushes);
}

TEST(firme_gm_gpi_geometry, reservation_does_not_block_standalone_transition)
{
	initialize(0, 0);
	struct firme_gm_reservation *reservation = NULL;
	uint64_t completed = 99;

	l1[0] = GPT_L1_NS_DESC;
	LONGS_EQUAL(0, firme_gm_reserve(1ULL << 30, &reservation));
	LONGS_EQUAL(0, firme_gm_gpi_set(1ULL << 30, 1,
		GPT_GPI_REALM, SMC_FROM_REALM, &completed));
	UNSIGNED_LONGLONGS_EQUAL(1, completed);
	UNSIGNED_LONGS_EQUAL(1, locks);
	firme_gm_release(&reservation);
}

TEST(firme_gm_gpi_geometry, rejects_invalid_standalone_requests)
{
	initialize(0, 0);
	uint64_t completed = 99;

	LONGS_EQUAL(-EINVAL, firme_gm_gpi_set(1ULL << 30, UINT64_MAX,
		GPT_GPI_SECURE, SMC_FROM_SECURE, &completed));
	LONGS_EQUAL(-EINVAL, firme_gm_gpi_set(1ULL << 30, 0,
		GPT_GPI_SECURE, SMC_FROM_SECURE, &completed));
	LONGS_EQUAL(-EINVAL, firme_gm_gpi_set(UINT64_MAX, 1,
		GPT_GPI_SECURE, SMC_FROM_SECURE, &completed));
	LONGS_EQUAL(-EINVAL, firme_gm_gpi_set(1ULL << 30, 1,
		GPT_GPI_SECURE, SMC_FROM_SECURE, NULL));
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
	UNSIGNED_LONGS_EQUAL(0, locks);
}

TEST(firme_gm_gpi_geometry, lifecycle_wrapper_completes_one_granule_per_call)
{
	const uint64_t base = 1ULL << 30;
	uint64_t completed = 99;

	initialize(0, 0);
	l1[0] = GPT_L1_NS_DESC;
	l1[1] = GPT_L1_NS_DESC;
	LONGS_EQUAL(0, firme_gm_reserve(base, &held_reservation));

	LONGS_EQUAL(0, firme_l1_gpt_gpi_set(base, 32, GPT_GPI_REALM, &completed));
	UNSIGNED_LONGLONGS_EQUAL(1, completed);
	UNSIGNED_LONGLONGS_EQUAL(0x999999999999999bULL, l1[0]);
	UNSIGNED_LONGLONGS_EQUAL(GPT_L1_NS_DESC, l1[1]);
	UNSIGNED_LONGS_EQUAL(1, locks);
	UNSIGNED_LONGS_EQUAL(1, unlocks);
	UNSIGNED_LONGS_EQUAL(0, active_mappings);
	UNSIGNED_LONGS_EQUAL(1, tlbis);
	UNSIGNED_LONGLONGS_EQUAL(base, tlbi_address);

	completed = 99;
	LONGS_EQUAL(0, firme_l1_gpt_gpi_set(base + 4096, 31,
		GPT_GPI_REALM, &completed));
	UNSIGNED_LONGLONGS_EQUAL(1, completed);
	UNSIGNED_LONGLONGS_EQUAL(0x99999999999999bbULL, l1[0]);
	UNSIGNED_LONGLONGS_EQUAL(GPT_L1_NS_DESC, l1[1]);
	UNSIGNED_LONGS_EQUAL(2, locks);
	UNSIGNED_LONGS_EQUAL(2, unlocks);
	UNSIGNED_LONGS_EQUAL(0, active_mappings);
	UNSIGNED_LONGS_EQUAL(2, tlbis);
	UNSIGNED_LONGLONGS_EQUAL(base + 4096, tlbi_address);

	/* The target reservation does not block donor transitions. */
	LONGS_EQUAL(0, firme_gm_gpi_set(base, 1, GPT_GPI_NS,
		SMC_FROM_REALM, &completed));
}

TEST(firme_gm_gpi_geometry, lifecycle_wrapper_preserves_worker_policy)
{
	uint64_t completed = 99;

	initialize(0, 0);
	l1[0] = GPT_L1_NS_DESC;
	LONGS_EQUAL(-EPERM, firme_l1_gpt_gpi_set(1ULL << 30, 1,
		GPT_GPI_ROOT, &completed));
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
	UNSIGNED_LONGLONGS_EQUAL(GPT_L1_NS_DESC, l1[0]);

	completed = 99;
	l1[0] = GPT_L1_ROOT_DESC;
	LONGS_EQUAL(-EPERM, firme_l1_gpt_gpi_set(1ULL << 30, 1,
		GPT_GPI_NS, &completed));
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
	UNSIGNED_LONGLONGS_EQUAL(GPT_L1_ROOT_DESC, l1[0]);
	UNSIGNED_LONGS_EQUAL(0, tlbis);
	UNSIGNED_LONGS_EQUAL(0, flushes);
}

TEST(firme_gm_gpi_geometry, lifecycle_wrapper_reports_descriptor_error)
{
	uint64_t completed = 99;

	initialize(0, 0);
	l0[1] = GPT_L0_BLK_DESC(GPT_GPI_NS);
	LONGS_EQUAL(-EINVAL, firme_l1_gpt_gpi_set(1ULL << 30, 1,
		GPT_GPI_REALM, &completed));
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
	UNSIGNED_LONGLONGS_EQUAL(GPT_L0_BLK_DESC(GPT_GPI_NS), l0[1]);
	UNSIGNED_LONGS_EQUAL(0, locks);
}

TEST(firme_gm_gpi_geometry, lifecycle_wrapper_validates_entire_range)
{
	const uint64_t invalid_requests[][2] = {
		{ 1ULL << 30, 0 },
		{ (1ULL << 30) + 1, 1 },
		{ 1ULL << 30, UINT64_MAX },
		{ UINT64_MAX - 4095, 1 },
		{ (1ULL << 52) - 4096, 2 }
	};

	initialize(0, 0);
	for (unsigned int index = 0; index < ARRAY_SIZE(invalid_requests);
	     index++) {
		uint64_t completed = 99;

		LONGS_EQUAL(-EINVAL, firme_l1_gpt_gpi_set(
			invalid_requests[index][0], invalid_requests[index][1],
			GPT_GPI_REALM, &completed));
		UNSIGNED_LONGLONGS_EQUAL(0, completed);
	}

	LONGS_EQUAL(-EINVAL, firme_l1_gpt_gpi_set(1ULL << 30, 1,
		GPT_GPI_REALM, NULL));
	UNSIGNED_LONGS_EQUAL(0, locks);
	UNSIGNED_LONGS_EQUAL(0, tlbis);
	UNSIGNED_LONGS_EQUAL(0, flushes);
}
