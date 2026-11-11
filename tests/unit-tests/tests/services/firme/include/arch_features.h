/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_TEST_ARCH_FEATURES_H
#define FIRME_TEST_ARCH_FEATURES_H

#include <stdbool.h>

bool is_feat_rme_supported(void);
bool is_feat_rme_gdi_supported(void);
bool is_feat_rme_gpc2_supported(void);
bool is_feat_mte2_supported(void);

#endif /* FIRME_TEST_ARCH_FEATURES_H */
