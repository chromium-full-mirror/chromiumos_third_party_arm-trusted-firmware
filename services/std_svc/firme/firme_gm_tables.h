/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_TABLES_H
#define FIRME_GM_TABLES_H

#include <stdint.h>

/*
 * Geometry must be initialized. The caller reserves the lifecycle target for
 * L0 inspection and publication. Donor access requires Root ownership.
 * These helpers do not manage mappings, reservations or ownership transitions.
 * Errors are negative errno values; output arguments change only on success.
 */
int firme_gm_tables_get_block(uint64_t target_addr, uint8_t *gpi);
int firme_gm_tables_get_donor(uint64_t target_addr, uint64_t *donor_addr);

/*
 * table_va is an aligned Root mapping of a geometrically valid donor, covering
 * the complete L1 table. Init requires a valid GPI and completes cache cleaning
 * before return. Validate is read-only and requires uniform effective GPIs.
 */
void firme_gm_tables_init(uintptr_t table_va, uint8_t gpi);
int firme_gm_tables_validate(uintptr_t table_va, uint8_t *gpi);

/*
 * Attach requires an initialized, cleaned, Root-owned donor. Detach requires
 * the GPI established by validation of expected_donor. The expected association
 * is checked before the L0 store; publication and full-PAR maintenance complete
 * before success, with no ordinary failure after publication starts.
 */
int firme_gm_tables_attach(uint64_t target_addr, uint64_t donor_addr,
			   uint8_t expected_gpi);
int firme_gm_tables_detach(uint64_t target_addr, uint64_t expected_donor,
			   uint8_t gpi);

/*
 * Zero a detached donor and complete VA cache maintenance. Keep it mapped and
 * Root-owned until return. The subsequent ownership transition must complete
 * PoPA maintenance, including MTE2 handling, before releasing the donor to NS.
 */
void firme_gm_tables_scrub(uintptr_t table_va);

#endif /* FIRME_GM_TABLES_H */
