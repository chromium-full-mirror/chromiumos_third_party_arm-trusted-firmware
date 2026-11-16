/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_PRIVATE_H
#define FIRME_GM_PRIVATE_H

#include <stddef.h>
#include <stdint.h>
#include <services/firme/firme_granule_mgmt.h>

void firme_gm_gpi_init(void);

/*
 * Stateless NS/Root transition provider. No caller-held donor locks or
 * reservations are required. Each call releases its locks and reports its
 * maintained prefix, including on error, with a generic errno status.
 * A short success requires a fresh suffix request. The provider owns PoPA
 * and MTE2 maintenance.
 */
int firme_l1_gpt_gpi_set(uint64_t base, uint64_t gcnt, uint8_t target_gpi,
			 uint64_t *gcnt_ret);

/* Read-only ownership check; each granule is checked under its GPT lock. */
int firme_gm_gpi_check(uint64_t base, uint64_t granule_count,
		       uint8_t expected_gpi);

#endif /* FIRME_GM_PRIVATE_H */
