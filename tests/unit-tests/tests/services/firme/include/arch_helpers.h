/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_TEST_ARCH_HELPERS_H
#define FIRME_TEST_ARCH_HELPERS_H

#include <stdint.h>

uint64_t read_gpccr_el3(void);
uint64_t read_gptbr_el3(void);

#endif /* FIRME_TEST_ARCH_HELPERS_H */
