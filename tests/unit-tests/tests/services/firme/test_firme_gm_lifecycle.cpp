/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <CppUTest/TestHarness.h>
#include <CppUTestExt/MockSupport.h>
#include <common/mock_debug.h>
#include <vector>

extern "C" {
#include <errno.h>
#include <string.h>
#include <arch.h>
#include <arch_helpers.h>
#include <lib/spinlock.h>
#include <plat/common/firme_gpt_policy.h>
#include <services/firme/firme_abi.h>

#include "firme_gm_geometry.h"
#include "firme_gm_lifecycle.h"
#include "firme_gm_mapping.h"
#include "firme_gm_private.h"
#include "firme_gm_reservation.h"
}

/* Real geometry, table operations and admission; only the backend is scripted. */
static uint64_t l0[8192] __aligned(65536);
static uint64_t storage[524288 + 16] __aligned(64);
static uint64_t *const table = storage + 8;
static uint8_t ownership[4096];
static uint64_t gpccr;
static uint64_t target_addr;
static uint64_t donor_addr;
static unsigned int mapped;
static unsigned int mapping_calls;
static unsigned int reserve_calls;
static unsigned int transition_locks;
static unsigned int transition_unlocks;
static unsigned int maintenance_calls;
static unsigned int published;
static unsigned int completed_tlbis;
static int mapping_error;
static bool initialized;
static bool corrupt_before_attach;
static bool fatal;
static bool policy_present;
static struct firme_gm_reservation *active_reservation;
static struct firme_gm_reservation *donor_target_reservation;
static firme_gpt_manager_range excluded_range;
static firme_gpt_policy policy;

struct transition_step {
	uint64_t offset;
	uint64_t requested;
	uint8_t target_gpi;
	uint64_t completed;
	int status;
};

static std::vector<transition_step> steps;
static size_t next_step;

static void check_words(const uint64_t *words, size_t count, uint64_t value)
{
	for (size_t index = 0; index < count; index++) {
		if (words[index] != value) {
			UNSIGNED_LONGLONGS_EQUAL(value, words[index]);
		}
	}
}

static void check_ownership(size_t first, size_t count, uint8_t gpi)
{
	for (size_t index = first; index < first + count; index++) {
		UNSIGNED_LONGS_EQUAL(gpi, ownership[index]);
	}
}

extern "C" {
char __RO_START__[1];
char __RO_END__[1];
char __RW_END__[1];

int __real_firme_gm_reserve(uint64_t base,
			    struct firme_gm_reservation **reservation);
void __real_firme_gm_release(struct firme_gm_reservation **reservation);
}

/* Probe admission after every backend return, when no transition lock exists. */
static void check_admission(void)
{
	struct firme_gm_reservation *competitor = NULL;
	uint64_t par_size = firme_gm_get_par_size();

	CHECK(active_reservation != NULL);
	UNSIGNED_LONGS_EQUAL(transition_locks, transition_unlocks);
	LONGS_EQUAL(-EINPROGRESS,
		__real_firme_gm_reserve(target_addr, &competitor));
	POINTERS_EQUAL(NULL, competitor);
	/* A donor PAR can independently be another lifecycle operation's target. */
	LONGS_EQUAL(donor_target_reservation == NULL ? 0 : -EINPROGRESS,
		__real_firme_gm_reserve(donor_addr & ~(par_size - 1), &competitor));
	__real_firme_gm_release(&competitor);
	uint64_t disjoint_addr = 0;

	while (disjoint_addr == target_addr ||
	       disjoint_addr == (donor_addr & ~(par_size - 1))) {
		disjoint_addr += par_size;
	}

	LONGS_EQUAL(0, __real_firme_gm_reserve(disjoint_addr,
					     &competitor));
	__real_firme_gm_release(&competitor);
}

extern "C" {
uint64_t read_gpccr_el3(void)
{
	return gpccr;
}

uint64_t read_gptbr_el3(void)
{
	return (uintptr_t)l0 >> 12;
}

bool is_feat_rme_gdi_supported(void)
{
	return false;
}

bool is_feat_rme_gpc2_supported(void)
{
	return false;
}

const struct firme_gpt_policy *plat_firme_get_gpt_policy(void)
{
	return policy_present ? &policy : NULL;
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

int __wrap_firme_gm_reserve(uint64_t base,
			    struct firme_gm_reservation **reservation)
{
	reserve_calls++;
	int status = __real_firme_gm_reserve(base, reservation);

	if (status == 0) {
		active_reservation = *reservation;
	}

	return status;
}

void __wrap_firme_gm_release(struct firme_gm_reservation **reservation)
{
	UNSIGNED_LONGS_EQUAL(0, mapped);
	UNSIGNED_LONGS_EQUAL(transition_locks, transition_unlocks);
	__real_firme_gm_release(reservation);
	active_reservation = NULL;
}

int firme_gm_map_root(uint64_t physical_addr, size_t size,
		      struct firme_gm_mapping *mapping)
{
	mapping_calls++;
	UNSIGNED_LONGLONGS_EQUAL(donor_addr, physical_addr);
	UNSIGNED_LONGLONGS_EQUAL(firme_gm_get_l1_table_size(), size);
	check_admission();
	if (mapping_error != 0) {
		return mapping_error;
	}

	UNSIGNED_LONGS_EQUAL(0, mapped);
	mapping->va = (uintptr_t)table;
	mapping->size = size;
	mapped++;
	return 0;
}

void firme_gm_unmap_root(struct firme_gm_mapping *mapping)
{
	if (mapping->size == 0) {
		return;
	}

	check_admission();
	UNSIGNED_LONGS_EQUAL(1, mapped);
	mapped--;
	*mapping = {};
	if (corrupt_before_attach) {
		l0[1] = 0x81;
	}
}

int firme_l1_gpt_gpi_set(uint64_t base, uint64_t granule_count,
			 uint8_t target_gpi, uint64_t *completed_granules)
{
	check_admission();
	CHECK(next_step < steps.size());
	const transition_step &step = steps[next_step++];
	uint64_t granule_size = firme_gm_get_granule_size();

	UNSIGNED_LONGLONGS_EQUAL(donor_addr + step.offset * granule_size, base);
	UNSIGNED_LONGLONGS_EQUAL(step.requested, granule_count);
	UNSIGNED_LONGS_EQUAL(step.target_gpi, target_gpi);
	transition_locks++;
	for (size_t index = step.offset;
	     index < step.offset + step.completed; index++) {
		UNSIGNED_LONGS_EQUAL(target_gpi == 10 ? 9 : 10, ownership[index]);
		ownership[index] = target_gpi;
	}

	*completed_granules = step.completed;
	transition_unlocks++;
	check_admission();
	return step.status;
}

void clean_dcache_range(uintptr_t address, size_t size)
{
	check_admission();
	UNSIGNED_LONGS_EQUAL(1, mapped);
	UNSIGNED_LONGLONGS_EQUAL((uintptr_t)table, address);
	UNSIGNED_LONGLONGS_EQUAL(firme_gm_get_l1_table_size(), size);
	check_ownership(0, firme_gm_get_l1_granule_count(), 10);
	check_words(table, size / 8, 0x9999999999999999ULL);
	UNSIGNED_LONGLONGS_EQUAL(0x91, l0[1]);
	initialized = true;
	maintenance_calls++;
}

void flush_dcache_range(uintptr_t address, size_t size)
{
	check_admission();
	UNSIGNED_LONGLONGS_EQUAL((uintptr_t)&l0[1], address);
	UNSIGNED_LONGLONGS_EQUAL(8, size);
	CHECK(initialized);
	UNSIGNED_LONGS_EQUAL(0, mapped);
	UNSIGNED_LONGLONGS_EQUAL(donor_addr | 3, l0[1]);
	published++;
	maintenance_calls++;
}

void dsboshst(void)
{
	check_admission();
	maintenance_calls++;
}

void dsbosh(void)
{
	check_admission();
	maintenance_calls++;
}

void test_tlbirpaos(uintptr_t address, unsigned long size)
{
	check_admission();
	UNSIGNED_LONGLONGS_EQUAL(target_addr, address);
	UNSIGNED_LONGS_EQUAL(firme_gm_get_par_size() == (1ULL << 30) ? 6 : 9,
			    size);
	UNSIGNED_LONGS_EQUAL(1, published);
	completed_tlbis++;
	maintenance_calls++;
}
}

static void configure_geometry(bool large = false)
{
	gpccr = large ? (6ULL << 0) | (1ULL << 14) | (9ULL << 20) : 0;
	LONGS_EQUAL(0, firme_gm_geometry_init());
	target_addr = firme_gm_get_par_size();
	donor_addr = 2 * target_addr;
	excluded_range = { 0, target_addr, false };
	policy = { true, &excluded_range, 1 };
	firme_gm_lifecycle_init();
	CHECK(firme_gm_lifecycle_is_supported());
	uintptr_t actual_l0;

	LONGS_EQUAL(0, firme_gm_get_l0_entry_address(target_addr, &actual_l0));
	UNSIGNED_LONGLONGS_EQUAL((uintptr_t)&l0[1], actual_l0);
}

TEST_GROUP(firme_gm_lifecycle) {
	TEST_SETUP()
	{
		memset(l0, 0x91, sizeof(l0));
		l0[1] = 0x91;
		memset(storage, 0xa5, sizeof(storage));
		memset(ownership, 9, sizeof(ownership));
		mapped = mapping_calls = reserve_calls = 0;
		transition_locks = transition_unlocks = maintenance_calls = 0;
		published = completed_tlbis = 0;
		mapping_error = 0;
		initialized = corrupt_before_attach = false;
		fatal = false;
		policy_present = true;
		active_reservation = NULL;
		donor_target_reservation = NULL;
		steps.clear();
		next_step = 0;
		configure_geometry();
	}

	TEST_TEARDOWN()
	{
		mock().checkExpectations();
		mock().clear();
		UNSIGNED_LONGS_EQUAL(transition_locks, transition_unlocks);
		if (fatal) {
			CHECK(active_reservation != NULL);
			__real_firme_gm_release(&active_reservation);
		} else {
			POINTERS_EQUAL(NULL, active_reservation);
			UNSIGNED_LONGS_EQUAL(0, mapped);
		}

		__real_firme_gm_release(&donor_target_reservation);
		UNSIGNED_LONGS_EQUAL(steps.size(), next_step);
		UNSIGNED_LONGLONGS_EQUAL(0x9191919191919191ULL, l0[0]);
		UNSIGNED_LONGLONGS_EQUAL(0x9191919191919191ULL, l0[2]);
		check_words(storage, 8, 0xa5a5a5a5a5a5a5a5ULL);
		check_words(table + firme_gm_get_l1_entry_count(), 8,
			    0xa5a5a5a5a5a5a5a5ULL);
	}
};

TEST(firme_gm_lifecycle, create_continues_fresh_suffixes_with_admission)
{
	steps = { { 0, 32, 10, 3, 0 }, { 3, 29, 10, 5, 0 },
		  { 8, 24, 10, 24, 0 } };
	LONGS_EQUAL(FIRME_SUCCESS, firme_gm_create(target_addr, donor_addr));
	UNSIGNED_LONGLONGS_EQUAL(donor_addr | 3, l0[1]);
	check_ownership(0, 32, 10);
	UNSIGNED_LONGS_EQUAL(1, published);
	UNSIGNED_LONGS_EQUAL(1, completed_tlbis);
}

TEST(firme_gm_lifecycle, create_rolls_back_exact_error_prefix_in_fresh_calls)
{
	steps = { { 0, 32, 10, 3, 0 }, { 3, 29, 10, 4, -EPERM },
		  { 0, 7, 9, 2, 0 }, { 2, 5, 9, 3, 0 }, { 5, 2, 9, 2, 0 } };
	LONGS_EQUAL(FIRME_DENIED, firme_gm_create(target_addr, donor_addr));
	check_ownership(0, 32, 9);
	check_words(table, 16384, 0xa5a5a5a5a5a5a5a5ULL);
	UNSIGNED_LONGS_EQUAL(0, maintenance_calls);
	UNSIGNED_LONGLONGS_EQUAL(0x91, l0[1]);
}

TEST(firme_gm_lifecycle, create_error_without_progress_needs_no_rollback)
{
	steps = { { 0, 32, 10, 0, -EPERM } };
	LONGS_EQUAL(FIRME_DENIED, firme_gm_create(target_addr, donor_addr));
	check_ownership(0, 32, 9);
	UNSIGNED_LONGS_EQUAL(0, maintenance_calls);
}

TEST(firme_gm_lifecycle, create_zero_progress_success_aborts_without_looping)
{
	steps = { { 0, 32, 10, 0, 0 } };
	LONGS_EQUAL(FIRME_ABORTED, firme_gm_create(target_addr, donor_addr));
	check_ownership(0, 32, 9);
}

TEST(firme_gm_lifecycle, create_attachment_failure_rolls_back_entire_donor)
{
	steps = { { 0, 32, 10, 32, 0 }, { 0, 32, 9, 11, 0 },
		  { 11, 21, 9, 21, 0 } };
	corrupt_before_attach = true;
	LONGS_EQUAL(FIRME_OP_CONFLICT, firme_gm_create(target_addr, donor_addr));
	check_ownership(0, 32, 9);
	UNSIGNED_LONGS_EQUAL(0, published);
}

TEST(firme_gm_lifecycle, create_terminal_rollback_error_is_fatal)
{
	steps = { { 0, 32, 10, 7, -EPERM }, { 0, 7, 9, 2, -EIO } };
	panic_environment_t environment;

	fatal = true;
	if (SETUP_PANIC_ENVIRONMENT(environment)) {
		firme_gm_create(target_addr, donor_addr);
		FAIL("CREATE returned after a terminal rollback failure");
	}

	check_ownership(0, 2, 9);
	check_ownership(2, 5, 10);
	check_ownership(7, 25, 9);
	check_admission();
	UNSIGNED_LONGS_EQUAL(0, mapped);
	UNSIGNED_LONGLONGS_EQUAL(0x91, l0[1]);
}

TEST(firme_gm_lifecycle, create_invalid_geometry_releases_target)
{
	uint64_t physical_limit = firme_gm_get_pps_size();
	uint64_t invalid_targets[] = { target_addr + 1, target_addr + 4096,
		physical_limit, physical_limit + target_addr,
		UINT64_MAX & ~(target_addr - 1) };
	uint64_t invalid_donors[] = { 0, donor_addr + 1, donor_addr + 4096,
		physical_limit, physical_limit + 131072,
		UINT64_MAX & ~131071ULL };

	for (uint64_t invalid_target : invalid_targets) {
		LONGS_EQUAL(FIRME_INVALID_PARAMETERS,
			firme_gm_create(invalid_target, donor_addr));
	}

	UNSIGNED_LONGS_EQUAL(0, reserve_calls);
	for (uint64_t invalid_donor : invalid_donors) {
		LONGS_EQUAL(FIRME_INVALID_PARAMETERS,
			firme_gm_create(target_addr, invalid_donor));
		POINTERS_EQUAL(NULL, active_reservation);
	}

	UNSIGNED_LONGS_EQUAL(ARRAY_SIZE(invalid_donors), reserve_calls);
	UNSIGNED_LONGS_EQUAL(0, mapping_calls);
}

TEST(firme_gm_lifecycle, create_policy_and_self_describing_rejections)
{
	LONGS_EQUAL(FIRME_DENIED, firme_gm_create(0, donor_addr));
	LONGS_EQUAL(FIRME_NO_ENTRY, firme_gm_create(target_addr, target_addr));
	policy_present = false;
	firme_gm_lifecycle_init();
	CHECK_FALSE(firme_gm_lifecycle_is_supported());
	LONGS_EQUAL(FIRME_NOT_SUPPORTED, firme_gm_create(target_addr, donor_addr));
	UNSIGNED_LONGS_EQUAL(1, reserve_calls);
}

TEST(firme_gm_lifecycle, create_rejects_existing_table_and_malformed_descriptor)
{
	l0[1] = donor_addr | 3;
	LONGS_EQUAL(FIRME_EXISTS, firme_gm_create(target_addr, donor_addr));
	l0[1] = 0x92;
	LONGS_EQUAL(FIRME_INVALID_PARAMETERS,
		firme_gm_create(target_addr, donor_addr));
	UNSIGNED_LONGS_EQUAL(0, mapping_calls);
}

TEST(firme_gm_lifecycle, create_mapping_failure_cleans_admission)
{
	mapping_error = -ENOMEM;
	LONGS_EQUAL(FIRME_NO_MEMORY, firme_gm_create(target_addr, donor_addr));
	UNSIGNED_LONGS_EQUAL(1, mapping_calls);
	UNSIGNED_LONGS_EQUAL(0, maintenance_calls);
}

TEST(firme_gm_lifecycle, create_target_conflict_has_no_donor_access)
{
	struct firme_gm_reservation *competitor = NULL;

	LONGS_EQUAL(0, __real_firme_gm_reserve(target_addr, &competitor));
	LONGS_EQUAL(FIRME_OP_CONFLICT, firme_gm_create(target_addr, donor_addr));
	__real_firme_gm_release(&competitor);
	UNSIGNED_LONGS_EQUAL(0, mapping_calls);
}

TEST(firme_gm_lifecycle, create_reservation_exhaustion_is_busy)
{
	struct firme_gm_reservation *competitors[4] = {};

	for (unsigned int index = 0; index < 4; index++) {
		LONGS_EQUAL(0, __real_firme_gm_reserve(index * 4096,
						     &competitors[index]));
	}

	LONGS_EQUAL(FIRME_BUSY, firme_gm_create(target_addr, donor_addr));
	for (auto &competitor : competitors) {
		__real_firme_gm_release(&competitor);
	}

	UNSIGNED_LONGS_EQUAL(0, mapping_calls);
}

TEST(firme_gm_lifecycle, create_does_not_reserve_donor_par)
{
	LONGS_EQUAL(0, __real_firme_gm_reserve(donor_addr,
					     &donor_target_reservation));
	steps = { { 0, 32, 10, 3, 0 }, { 3, 29, 10, 29, 0 } };
	LONGS_EQUAL(FIRME_SUCCESS, firme_gm_create(target_addr, donor_addr));
	check_ownership(0, 32, 10);
}

TEST(firme_gm_lifecycle, create_large_real_geometry_covers_complete_table)
{
	configure_geometry(true);
	UNSIGNED_LONGLONGS_EQUAL(1ULL << 52, firme_gm_get_pps_size());
	UNSIGNED_LONGLONGS_EQUAL(1ULL << 39, firme_gm_get_par_size());
	UNSIGNED_LONGS_EQUAL(65536, firme_gm_get_granule_size());
	UNSIGNED_LONGS_EQUAL(4194304, firme_gm_get_l1_table_size());
	UNSIGNED_LONGLONGS_EQUAL(64, firme_gm_get_l1_granule_count());
	steps = { { 0, 64, 10, 64, 0 } };
	LONGS_EQUAL(FIRME_SUCCESS, firme_gm_create(target_addr, donor_addr));
	UNSIGNED_LONGS_EQUAL(1, completed_tlbis);
}
