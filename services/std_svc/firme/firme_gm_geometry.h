/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_GEOMETRY_H
#define FIRME_GM_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

/* Initialize once, before consumers run. Geometry is immutable thereafter. */
int firme_gm_geometry_init(void);
uint64_t firme_gm_geometry_feature_register(void);

uint64_t firme_gm_get_pps_size(void);
uint64_t firme_gm_get_par_size(void);
size_t firme_gm_get_granule_size(void);
size_t firme_gm_get_l1_table_size(void);
uint64_t firme_gm_get_l1_granule_count(void);
uint64_t firme_gm_get_l1_entry_count(void);

/* Geometry only: these do not check policy, live GPT state or mappings. */
int firme_gm_validate_par(uint64_t target_addr);
int firme_gm_validate_l1_object(uint64_t donor_addr);

/* Validate the target and return its L0 descriptor address without access. */
int firme_gm_get_l0_entry_address(uint64_t target_addr, uintptr_t *address);

/* The caller must supply a valid granule address within the initialized PPS. */
uint64_t firme_gm_get_l1_entry_index(uint64_t pa);
unsigned int firme_gm_get_l1_gpi_shift(uint64_t pa);
size_t firme_gm_get_l1_entries_per_2mb(void);

#endif /* FIRME_GM_GEOMETRY_H */
