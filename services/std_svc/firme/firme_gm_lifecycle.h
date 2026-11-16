/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_LIFECYCLE_H
#define FIRME_GM_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

void firme_gm_lifecycle_init(void);
bool firme_gm_lifecycle_is_supported(void);

/* Synchronous lifecycle operations return FIRME status values. */
int firme_gm_create(uint64_t target_addr, uint64_t donor_addr);

#endif /* FIRME_GM_LIFECYCLE_H */
