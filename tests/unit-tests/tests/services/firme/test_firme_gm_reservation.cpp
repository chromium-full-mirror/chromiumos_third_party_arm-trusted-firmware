/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <CppUTest/TestHarness.h>
#include <CppUTestExt/MockSupport.h>
#include <common/mock_debug.h>

extern "C" {
#include <errno.h>
#include <stdint.h>
#include <lib/spinlock.h>

#include "firme_gm_reservation.h"
}

static unsigned int try_locks;
static unsigned int blocking_locks;
static unsigned int acquired_locks;
static unsigned int unlocks;
static bool try_lock_busy;
static struct firme_gm_reservation *handles[FIRME_GM_RESERVATIONS_MAX + 1];

extern "C" {
/* Record lock ownership so leaked locks and recursive acquisition fail. */
void spin_lock(spinlock_t *lock)
{
	UNSIGNED_LONGS_EQUAL(0, lock->lock);
	lock->lock = 1;
	blocking_locks++;
	acquired_locks++;
}

bool spin_trylock(spinlock_t *lock)
{
	try_locks++;
	if (try_lock_busy) {
		return false;
	}
	UNSIGNED_LONGS_EQUAL(0, lock->lock);
	lock->lock = 1;
	acquired_locks++;
	return true;
}

void spin_unlock(spinlock_t *lock)
{
	UNSIGNED_LONGS_EQUAL(1, lock->lock);
	lock->lock = 0;
	unlocks++;
}
}

static int reserve(unsigned int slot, uint64_t target_addr)
{
	return firme_gm_reserve(target_addr, &handles[slot]);
}

TEST_GROUP(firme_gm_reservation) {
	TEST_SETUP()
	{
		try_locks = 0;
		blocking_locks = 0;
		acquired_locks = 0;
		unlocks = 0;
		try_lock_busy = false;
		for (unsigned int i = 0; i <= FIRME_GM_RESERVATIONS_MAX; i++) {
			handles[i] = NULL;
		}
	}

	TEST_TEARDOWN()
	{
		try_lock_busy = false;
		for (unsigned int i = 0; i <= FIRME_GM_RESERVATIONS_MAX; i++) {
			firme_gm_release(&handles[i]);
		}
		UNSIGNED_LONGS_EQUAL(acquired_locks, unlocks);
		mock().checkExpectations();
		mock().clear();
	}
};

#if !FIRME_GM_RESERVATION_CAPACITY_ONLY
TEST(firme_gm_reservation, rejects_missing_output_without_locking)
{
	LONGS_EQUAL(-EINVAL, firme_gm_reserve(0, NULL));
	UNSIGNED_LONGS_EQUAL(0, try_locks);
	UNSIGNED_LONGS_EQUAL(0, blocking_locks);
}

TEST(firme_gm_reservation, excludes_only_the_same_target)
{
	LONGS_EQUAL(0, reserve(0, 0));
	LONGS_EQUAL(-EINPROGRESS, reserve(1, 0));
	POINTERS_EQUAL(NULL, handles[1]);
	LONGS_EQUAL(0, reserve(1, 1));
	LONGS_EQUAL(0, reserve(2, UINT64_MAX));
	LONGS_EQUAL(-EINPROGRESS, reserve(3, UINT64_MAX));
	UNSIGNED_LONGS_EQUAL(0, blocking_locks);
}

TEST(firme_gm_reservation, acquire_does_not_overwrite_an_existing_handle)
{
	LONGS_EQUAL(0, reserve(0, 100));
	struct firme_gm_reservation *original = handles[0];

	LONGS_EQUAL(-EINVAL, reserve(0, 300));
	POINTERS_EQUAL(original, handles[0]);
	LONGS_EQUAL(-EINPROGRESS, reserve(1, 100));
	LONGS_EQUAL(0, reserve(1, 300));
}

TEST(firme_gm_reservation, handle_can_be_handed_to_another_caller)
{
	LONGS_EQUAL(0, reserve(0, 100));
	handles[1] = handles[0];
	handles[0] = NULL;
	LONGS_EQUAL(-EINPROGRESS, reserve(0, 100));
	firme_gm_release(&handles[1]);
	POINTERS_EQUAL(NULL, handles[1]);
	LONGS_EQUAL(0, reserve(2, 100));
}

TEST(firme_gm_reservation, invalid_nonnull_release_panics)
{
	uint64_t unrelated = 0;

	LONGS_EQUAL(0, reserve(0, 100));
	struct firme_gm_reservation *invalid[] = {
		(struct firme_gm_reservation *)(uintptr_t)1,
		(struct firme_gm_reservation *)UINTPTR_MAX,
		(struct firme_gm_reservation *)&unrelated,
		(struct firme_gm_reservation *)((uintptr_t)handles[0] + 1),
	};

	for (auto handle : invalid) {
		panic_environment_t env;

		if (SETUP_PANIC_ENVIRONMENT(env)) {
			firme_gm_release(&handle);
			FAIL("invalid release returned");
		}
	}
	UNSIGNED_LONGS_EQUAL(0, blocking_locks);
}

TEST(firme_gm_reservation, inactive_handle_is_rejected_before_reuse)
{
	LONGS_EQUAL(0, reserve(0, 100));
	struct firme_gm_reservation *stale = handles[0];

	firme_gm_release(&handles[0]);
	POINTERS_EQUAL(NULL, handles[0]);
	panic_environment_t env;

	if (SETUP_PANIC_ENVIRONMENT(env)) {
		firme_gm_release(&stale);
		FAIL("inactive release returned");
	}
}

TEST(firme_gm_reservation, release_allows_reuse_and_null_cleanup)
{
	firme_gm_release(NULL);
	firme_gm_release(&handles[0]);
	UNSIGNED_LONGS_EQUAL(0, acquired_locks);
	LONGS_EQUAL(0, reserve(0, 100));
	firme_gm_release(&handles[0]);
	firme_gm_release(&handles[0]);
	LONGS_EQUAL(0, reserve(1, 100));
}

TEST(firme_gm_reservation, lock_contention_rejects_acquisition_without_waiting)
{
	try_lock_busy = true;
	LONGS_EQUAL(-EBUSY, reserve(0, 100));
	POINTERS_EQUAL(NULL, handles[0]);
	UNSIGNED_LONGS_EQUAL(1, try_locks);
	UNSIGNED_LONGS_EQUAL(0, blocking_locks);
	UNSIGNED_LONGS_EQUAL(0, unlocks);
	try_lock_busy = false;
	LONGS_EQUAL(0, reserve(0, 100));
}
#endif

TEST(firme_gm_reservation, registry_exhaustion_and_reuse_include_highest_slot)
{
	const unsigned int spare = FIRME_GM_RESERVATIONS_MAX;
	const unsigned int last = spare - 1;

	for (unsigned int i = 0; i < spare; i++) {
		LONGS_EQUAL(0, reserve(i, i * 100));
	}
	LONGS_EQUAL(-EBUSY, reserve(spare, 10000));
	POINTERS_EQUAL(NULL, handles[spare]);
	firme_gm_release(&handles[last]);
	LONGS_EQUAL(0, reserve(spare, 10000));
	LONGS_EQUAL(-EBUSY, reserve(last, last * 100));
	firme_gm_release(&handles[spare]);
	LONGS_EQUAL(0, reserve(last, last * 100));
}
