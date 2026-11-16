/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>

#include "firme_gm_geometry.h"
#include "firme_gm_lifecycle.h"
#include "firme_gm_private.h"
#include "firme_private.h"

#include <arch.h>
#include <arch_features.h>
#include <common/debug.h>
#include <lib/extensions/rme.h>
#include <lib/smccc.h>
#include <services/firme/firme_granule_mgmt.h>
#include <services/firme_svc.h>
#include <smccc_helpers.h>

/* Lifecycle capabilities are reported only for the Realm instance. */
static uint64_t registers[FIRME_GRANULE_MGMT_FEATURE_REG_COUNT] = {
	FIRME_GM_GPI_SET_BIT, 0U
};


#if ENABLE_RMM
#define GRANULE_MGMT_INSTANCE_SUPPORT \
	(BIT(FIRME_SECURE) | BIT(FIRME_NONSECURE) | BIT(FIRME_REALM))
#else
#define GRANULE_MGMT_INSTANCE_SUPPORT \
	(BIT(FIRME_SECURE) | BIT(FIRME_NONSECURE))
#endif /* ENABLE_RMM */


static int32_t firme_granule_mgmt_service_init(void)
{
	int rc;

	registers[1] = 0U;

	if (is_feat_rme_supported()) {
		rc = firme_gm_geometry_init();
		if (rc != 0) {
			return rc;
		}

		registers[1] = firme_gm_geometry_feature_register();

		firme_gm_gpi_init();
		firme_gm_lifecycle_init();
	}

	return FIRME_SUCCESS;
}

static int32_t
firme_granule_mgmt_service_version(firme_instance_e instance __unused)
{
	return FIRME_VERSION(FIRME_GRANULE_MGMT_VERSION_MAJOR,
			     FIRME_GRANULE_MGMT_VERSION_MINOR);
}

static bool firme_granule_mgmt_service_is_supported(firme_instance_e instance)
{
	return is_feat_rme_supported() &&
	       (GRANULE_MGMT_INSTANCE_SUPPORT & BIT(instance)) != 0U;
}

static int32_t
firme_granule_mgmt_service_get_feature_reg(firme_instance_e instance,
					   uint8_t reg_index, uint64_t *reg)
{
	if (reg == NULL) {
		return FIRME_INVALID_PARAMETERS;
	}

	if (reg_index >= FIRME_GRANULE_MGMT_FEATURE_REG_COUNT) {
		return FIRME_NOT_SUPPORTED;
	}

	*reg = registers[reg_index];
	if ((reg_index == 0U) && (instance == FIRME_REALM) &&
	    firme_gm_lifecycle_is_supported()) {
		*reg |= FIRME_GM_L1_CREATE_BIT;
	}

	return FIRME_SUCCESS;
}

u_register_t firme_granule_mgmt_service_handler(firme_instance_e instance,
						uint32_t smc_fid, uint64_t x1,
						uint64_t x2, uint64_t x3,
						uint64_t x4, void *cookie,
						void *handle, uint64_t flags)
{
	if (!is_feat_rme_supported()) {
		SMC_RET1(handle, FIRME_NOT_SUPPORTED);
	}

	switch (smc_fid) {
	case FIRME_GM_L1_GPT_CREATE_FID:
		if (instance != FIRME_REALM) {
			SMC_RET1(handle, FIRME_NOT_SUPPORTED);
		}

		SMC_RET1(handle, firme_gm_create(x1, x2));
	case FIRME_GM_GPI_SET_FID: {
		int ret;
		uint64_t gcnt = 0;

		ret = firme_gm_gpi_set(x1, x2, x3, flags, &gcnt);
		switch (ret) {
		case 0:
			SMC_RET2(handle, FIRME_SUCCESS, gcnt);
		case -EINVAL:
			SMC_RET2(handle, FIRME_INVALID_PARAMETERS, 0);
		case -EPERM:
			SMC_RET2(handle, FIRME_DENIED, 0);
		case -EINPROGRESS:
			SMC_RET2(handle, FIRME_OP_CONFLICT, 0);
		case -EBUSY:
		case -ENOMEM:
			SMC_RET2(handle, FIRME_BUSY, 0);
		default:
			SMC_RET2(handle, FIRME_NOT_SUPPORTED, 0);
		}
		break;
	}
	default:
		ERROR("FIRME Granule Management Service FID 0x%X not implemented\n",
		      smc_fid);
		SMC_RET1(handle, FIRME_NOT_SUPPORTED);
	}
}

const struct firme_service firme_granule_mgmt_service = {
	.id = FIRME_GRANULE_MGMT_ID,
	.init = firme_granule_mgmt_service_init,
	.version = firme_granule_mgmt_service_version,
	.is_supported = firme_granule_mgmt_service_is_supported,
	.get_feature_reg = firme_granule_mgmt_service_get_feature_reg,
	.call = firme_granule_mgmt_service_handler,
};
