/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <lib/spinlock.h>
#include <lib/utils_def.h>

#include "firme_gm_reservation.h"

#if (FIRME_GM_RESERVATIONS_MAX < 1) || (FIRME_GM_RESERVATIONS_MAX > 64)
#error "FIRME_GM_RESERVATIONS_MAX must be between 1 and 64"
#endif

#define FIRME_GM_VALID_SLOTS \
	GENMASK_64(FIRME_GM_RESERVATIONS_MAX - 1U, 0U)

struct firme_gm_reservation {
	uint64_t target_addr;
};

static struct firme_gm_reservation reservations[FIRME_GM_RESERVATIONS_MAX];
static uint64_t active_slots;
static spinlock_t reservation_lock;

/* Validate registry membership without dereferencing the supplied handle. */
static bool reservation_index(const struct firme_gm_reservation *reservation,
			      unsigned int *index)
{
	uintptr_t offset = (uintptr_t)reservation - (uintptr_t)reservations;

	if ((offset >= sizeof(reservations)) ||
	    ((offset % sizeof(reservations[0])) != 0U)) {
		return false;
	}

	*index = (unsigned int)(offset / sizeof(reservations[0]));
	return true;
}

/* The remaining private helpers require reservation_lock. */
static bool reservation_active(unsigned int index)
{
	return (active_slots & BIT_64(index)) != 0U;
}

static bool target_reserved(uint64_t target_addr)
{
	uint64_t candidates = active_slots;

	while (candidates != 0U) {
		unsigned int index = (unsigned int)__builtin_ctzll(candidates);

		candidates &= candidates - 1U;
		if (reservations[index].target_addr == target_addr) {
			return true;
		}
	}

	return false;
}

int firme_gm_reserve(uint64_t target_addr,
		     struct firme_gm_reservation **reservation)
{
	uint64_t free_slots;
	unsigned int index;
	int rc = 0;

	if ((reservation == NULL) || (*reservation != NULL)) {
		return -EINVAL;
	}

	if (!spin_trylock(&reservation_lock)) {
		return -EBUSY;
	}

	free_slots = ~active_slots & FIRME_GM_VALID_SLOTS;
	if (free_slots == 0U) {
		rc = -EBUSY;
		goto out;
	}

	if (target_reserved(target_addr)) {
		rc = -EINPROGRESS;
		goto out;
	}

	index = (unsigned int)__builtin_ctzll(free_slots);
	reservations[index].target_addr = target_addr;

	active_slots |= BIT_64(index);
	*reservation = &reservations[index];
out:
	spin_unlock(&reservation_lock);
	return rc;
}

void firme_gm_release(struct firme_gm_reservation **reservation)
{
	unsigned int index;

	if ((reservation == NULL) || (*reservation == NULL)) {
		return;
	}
	if (!reservation_index(*reservation, &index)) {
		panic();
	}

	spin_lock(&reservation_lock);
	if (!reservation_active(index)) {
		spin_unlock(&reservation_lock);
		panic();
	}

	active_slots &= ~BIT_64(index);
	reservations[index].target_addr = 0U;
	*reservation = NULL;
	spin_unlock(&reservation_lock);
}
