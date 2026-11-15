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

#include "firme_gm_geometry.h"
#include "firme_gm_private.h"
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
	return false;
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
	locks++;
}

void firme_gpt_unlock(uint64_t base)
{
	(void)base;
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
	flush_dcache_to_popa_range(address, size);
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
	TEST_SETUP()
	{
		memset(l0, 0, sizeof(l0));
		memset(l1, 0, sizeof(l1));
		register_reads = 0;
		locks = 0;
		unlocks = 0;
		tlbis = 0;
		flushes = 0;
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

	UNSIGNED_LONGS_EQUAL((uint32_t)-EINVAL,
		firme_gm_gpi_set((1ULL << 39) + 1, 1, GPT_GPI_SECURE,
				 SMC_FROM_SECURE, &completed));
	UNSIGNED_LONGS_EQUAL(0, locks);
	UNSIGNED_LONGS_EQUAL(0, tlbis);
	UNSIGNED_LONGS_EQUAL(0, flushes);
	UNSIGNED_LONGLONGS_EQUAL(0, completed);
}
