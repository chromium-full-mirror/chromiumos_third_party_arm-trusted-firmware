/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_GM_MAPPING_H
#define FIRME_GM_MAPPING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct firme_gm_mapping {
	uintptr_t va;
	size_t size;
	bool dynamic;
};

/* Mapping does not authorize access before the memory becomes Root-owned. */
int firme_gm_map_root(uint64_t physical_addr, size_t size,
		      struct firme_gm_mapping *mapping);
void firme_gm_unmap_root(struct firme_gm_mapping *mapping);

#endif /* FIRME_GM_MAPPING_H */
