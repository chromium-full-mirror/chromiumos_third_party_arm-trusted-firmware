/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <lib/extensions/rme.h>
#include <lib/utils_def.h>
#include <plat/common/firme_gpt_policy.h>
#include <services/firme/firme_abi.h>

#include "firme_gm_geometry.h"
#include "firme_gm_lifecycle.h"
#include "firme_gm_mapping.h"
#include "firme_gm_private.h"
#include "firme_gm_reservation.h"
#include "firme_gm_tables.h"

static const struct firme_gpt_policy *gpt_policy;

#pragma weak plat_firme_get_gpt_policy
const struct firme_gpt_policy *plat_firme_get_gpt_policy(void)
{
	return NULL;
}

void firme_gm_lifecycle_init(void)
{
	const struct firme_gpt_policy *policy = plat_firme_get_gpt_policy();
	uint64_t previous_top = 0U;
	uint64_t par_size = firme_gm_get_par_size();

	gpt_policy = NULL;
#if !defined(PLAT_XLAT_TABLES_DYNAMIC) || !PLAT_XLAT_TABLES_DYNAMIC
	return;
#endif

	if ((policy == NULL) || (par_size == 0U) ||
	    (firme_gm_get_l1_granule_count() == 0U) ||
	    ((policy->exceptions == NULL) != (policy->exception_count == 0U))) {
		return;
	}

	for (size_t index = 0U; index < policy->exception_count; index++) {
		const struct firme_gpt_manager_range *range =
			&policy->exceptions[index];

		if ((range->top <= range->base) ||
		    !is_aligned(range->base, par_size) ||
		    !is_aligned(range->top, par_size) ||
		    (range->top > firme_gm_get_pps_size()) ||
		    (range->base < previous_top)) {
			return;
		}

		previous_top = range->top;
	}

	gpt_policy = policy;
}

bool firme_gm_lifecycle_is_supported(void)
{
	return gpt_policy != NULL;
}

static int prepare_target(uint64_t target_addr)
{
	bool host_managed;

	if (!firme_gm_lifecycle_is_supported()) {
		return FIRME_NOT_SUPPORTED;
	}

	if (firme_gm_validate_par(target_addr) != 0) {
		return FIRME_INVALID_PARAMETERS;
	}

	host_managed = gpt_policy->default_host_managed;
	for (size_t index = 0U; index < gpt_policy->exception_count; index++) {
		const struct firme_gpt_manager_range *range =
			&gpt_policy->exceptions[index];

		if ((target_addr >= range->base) && (target_addr < range->top)) {
			host_managed = range->host_managed;
			break;
		}
	}

	return host_managed ? FIRME_SUCCESS : FIRME_DENIED;
}

static int lifecycle_status(int error)
{
	switch (error) {
	case 0:
		return FIRME_SUCCESS;
	case -EINVAL:
		return FIRME_INVALID_PARAMETERS;
	case -EPERM:
	case -EACCES:
		return FIRME_DENIED;
	case -EBUSY:
	case -EAGAIN:
		return FIRME_BUSY;
	case -ENOMEM:
		return FIRME_NO_MEMORY;
	case -EINPROGRESS:
	case -ESTALE:
		return FIRME_OP_CONFLICT;
	default:
		return FIRME_ABORTED;
	}
}

static int transition_donor(uint64_t donor_addr, uint64_t granule_count,
			    uint8_t target_gpi, uint64_t *completed_granules)
{
	*completed_granules = 0U;
	while (*completed_granules < granule_count) {
		uint64_t remaining_granules = granule_count - *completed_granules;
		uint64_t next_addr = donor_addr + *completed_granules *
				    firme_gm_get_granule_size();
		uint64_t transitioned_granules = 0U;
		int rc;

		rc = firme_l1_gpt_gpi_set(next_addr, remaining_granules,
					target_gpi, &transitioned_granules);
		if (transitioned_granules > remaining_granules) {
			panic();
		}

		*completed_granules += transitioned_granules;
		if (rc != 0) {
			return rc;
		}

		/* The stateless backend must make progress on a successful call. */
		if (transitioned_granules == 0U) {
			return -EIO;
		}
	}

	return 0;
}

int firme_gm_create(uint64_t target_addr, uint64_t donor_addr)
{
	struct firme_gm_reservation *reservation = NULL;
	struct firme_gm_mapping mapping = { 0 };
	uint64_t completed_granules = 0U;
	uint64_t existing_donor;
	uint64_t donor_par;
	uint8_t original_gpi;
	int status;
	int rc;

	status = prepare_target(target_addr);
	if (status != FIRME_SUCCESS) {
		return status;
	}

	rc = firme_gm_reserve(target_addr, &reservation);
	if (rc != 0) {
		return lifecycle_status(rc);
	}

	if (firme_gm_tables_get_donor(target_addr, &existing_donor) == 0) {
		status = FIRME_EXISTS;
		goto out;
	}

	if (firme_gm_validate_l1_object(donor_addr) != 0) {
		status = FIRME_INVALID_PARAMETERS;
		goto out;
	}

	donor_par = round_down(donor_addr, firme_gm_get_par_size());
	if (donor_par == target_addr) {
		status = FIRME_NO_ENTRY;
		goto out;
	}

	rc = firme_gm_tables_get_block(target_addr, &original_gpi);
	if (rc != 0) {
		status = FIRME_INVALID_PARAMETERS;
		goto out;
	}

	/*
	 * Acquire the mapping before changing ownership so allocation failure
	 * needs no rollback. Do not access it until the whole donor is Root.
	 */
	rc = firme_gm_map_root(donor_addr, firme_gm_get_l1_table_size(), &mapping);
	if (rc != 0) {
		status = FIRME_NO_MEMORY;
		goto out;
	}

	rc = transition_donor(donor_addr, firme_gm_get_l1_granule_count(),
			      GPT_GPI_ROOT, &completed_granules);
	if (rc != 0) {
		status = lifecycle_status(rc);
		goto rollback;
	}

	/*
	 * Preserve the block's GPI in every L1 entry and clean the whole table
	 * before making it reachable through L0.
	 */
	firme_gm_tables_init(mapping.va, original_gpi);
	firme_gm_unmap_root(&mapping);

	rc = firme_gm_tables_attach(target_addr, donor_addr, original_gpi);
	status = lifecycle_status(rc);
	if (rc == 0) {
		goto out;
	}

rollback:
	/*
	 * Even a failed transition may complete a prefix. Restore exactly the
	 * accumulated prefix; an attach failure restores the entire donor.
	 */
	firme_gm_unmap_root(&mapping);
	if (completed_granules != 0U) {
		uint64_t rolled_back_granules;

		rc = transition_donor(donor_addr, completed_granules, GPT_GPI_NS,
				      &rolled_back_granules);
		if (rc != 0) {
			/* The completed prefix must not remain Root-owned. */
			panic();
		}
	}

out:
	firme_gm_unmap_root(&mapping);
	firme_gm_release(&reservation);
	return status;
}

struct firme_gm_destroy_result firme_gm_destroy(uint64_t target_addr)
{
	struct firme_gm_destroy_result result = {
		.status = prepare_target(target_addr),
	};
	struct firme_gm_reservation *reservation = NULL;
	struct firme_gm_mapping mapping = { 0 };
	uint64_t donor_addr;
	uint64_t donor_par;
	uint64_t completed_granules;
	uint8_t common_gpi;
	int rc;

	if (result.status != FIRME_SUCCESS) {
		return result;
	}

	rc = firme_gm_reserve(target_addr, &reservation);
	if (rc != 0) {
		result.status = lifecycle_status(rc);
		return result;
	}

	if (firme_gm_tables_get_donor(target_addr, &donor_addr) != 0) {
		result.status =
			(firme_gm_tables_get_block(target_addr, &common_gpi) == 0) ?
			FIRME_NO_ENTRY : FIRME_INVALID_PARAMETERS;
		goto out;
	}

	if (firme_gm_validate_l1_object(donor_addr) != 0) {
		result.status = FIRME_INVALID_PARAMETERS;
		goto out;
	}

	donor_par = round_down(donor_addr, firme_gm_get_par_size());
	if (donor_par == target_addr) {
		result.status = FIRME_DENIED;
		goto out;
	}

	/* Prove ownership before accessing donor contents through a Root VA. */
	rc = firme_gm_gpi_check(donor_addr, firme_gm_get_l1_granule_count(),
				GPT_GPI_ROOT);
	if (rc != 0) {
		result.status = FIRME_DENIED;
		goto out;
	}

	rc = firme_gm_map_root(donor_addr, firme_gm_get_l1_table_size(), &mapping);
	if (rc != 0) {
		result.status = ((rc == -EPERM) || (rc == -EACCES)) ?
				FIRME_DENIED : FIRME_BUSY;
		goto out;
	}

	/* Collapse is safe only if every entry has the same effective GPI. */
	if (firme_gm_tables_validate(mapping.va, &common_gpi) != 0) {
		result.status = FIRME_DENIED;
		goto out;
	}

	/*
	 * Detach checks the donor association and completes GPT invalidation
	 * before returning, so no walker can use the table when it is scrubbed.
	 */
	rc = firme_gm_tables_detach(target_addr, donor_addr, common_gpi);
	if (rc != 0) {
		result.status = lifecycle_status(rc);
		goto out;
	}

	/*
	 * Scrub while the detached donor is still Root-owned, then drop its
	 * mapping before returning it to NS. Keep the target reserved across each
	 * stateless release call and return the donor only after all complete.
	 */
	firme_gm_tables_scrub(mapping.va);
	firme_gm_unmap_root(&mapping);
	rc = transition_donor(donor_addr, firme_gm_get_l1_granule_count(),
			      GPT_GPI_NS, &completed_granules);
	if (rc != 0) {
		/* Scrubbing prevents reattachment; partial release is fatal. */
		panic();
	}

	result.status = FIRME_SUCCESS;
	result.donor_addr = donor_addr;
out:
	firme_gm_unmap_root(&mapping);
	firme_gm_release(&reservation);
	return result;
}
