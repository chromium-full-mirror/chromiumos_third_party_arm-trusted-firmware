/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <lib/spinlock.h>
#include <lib/utils_def.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <plat/common/firme_gpt_policy.h>

#include "firme_gm_mapping.h"

static spinlock_t firme_gm_mapping_lock __unused;

#pragma weak plat_firme_get_static_root_mapping
int plat_firme_get_static_root_mapping(uint64_t physical_addr, size_t size,
				       uintptr_t *virtual_addr)
{
	(void)physical_addr;
	(void)size;
	(void)virtual_addr;
	return -ENOENT;
}

int firme_gm_map_root(uint64_t physical_addr, size_t size,
			struct firme_gm_mapping *mapping)
{
	int rc;

	if ((mapping == NULL) || (size == 0U)) {
		return -EINVAL;
	}

	mapping->va = 0U;
	mapping->size = 0U;
	mapping->dynamic = false;
	rc = plat_firme_get_static_root_mapping(physical_addr, size, &mapping->va);
	if (rc == 0) {
		if (mapping->va == 0U) {
			return -EINVAL;
		}

		mapping->size = size;
		return 0;
	}

	if (rc != -ENOENT) {
		mapping->va = 0U;
		return rc;
	}

	mapping->va = 0U;

#if defined(PLAT_XLAT_TABLES_DYNAMIC) && PLAT_XLAT_TABLES_DYNAMIC
	spin_lock(&firme_gm_mapping_lock);
	rc = mmap_add_dynamic_region_alloc_va(
		physical_addr, &mapping->va, size,
		MT_MEMORY | MT_RW | MT_ROOT | MT_EXECUTE_NEVER);
	spin_unlock(&firme_gm_mapping_lock);
	if (rc != 0) {
		return rc;
	}

	mapping->size = size;
	mapping->dynamic = true;
	return 0;
#else
	/* Preserve the legacy identity-mapped GPT access path. */
	mapping->va = (uintptr_t)physical_addr;
	mapping->size = size;
	return 0;
#endif
}

void firme_gm_unmap_root(struct firme_gm_mapping *mapping)
{
	int rc;

	if ((mapping == NULL) || (mapping->size == 0U)) {
		return;
	}

#if defined(PLAT_XLAT_TABLES_DYNAMIC) && PLAT_XLAT_TABLES_DYNAMIC
	if (mapping->dynamic) {
		spin_lock(&firme_gm_mapping_lock);
		rc = mmap_remove_dynamic_region(mapping->va, mapping->size);
		spin_unlock(&firme_gm_mapping_lock);
		if (rc != 0) {
			ERROR("FIRME: failed to remove Root mapping (%d)\n", rc);
			panic();
		}
	}
#else
	(void)rc;
#endif

	mapping->va = 0U;
	mapping->size = 0U;
	mapping->dynamic = false;
}
