/*
 * Copyright (c) 2026, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FIRME_TEST_ARCH_HELPERS_H
#define FIRME_TEST_ARCH_HELPERS_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned long u_register_t;
typedef unsigned __int128 uint128_t;

uint64_t read_gpccr_el3(void);
uint64_t read_gptbr_el3(void);
uint64_t read_sctlr_el3(void);
void dsbosh(void);
void dsboshst(void);
void clean_dcache_range(uintptr_t address, size_t size);
void flush_dcache_range(uintptr_t address, size_t size);
void test_tlbirpaos(uintptr_t address, unsigned long size);
#define TLBIRPAOS(address, size) test_tlbirpaos(address, size)
#define TLBI_SZ_1G	6UL
#define TLBI_SZ_16G	7UL
#define TLBI_SZ_64G	8UL
#define TLBI_SZ_512G	9UL
void tlbirpalos_4k(uintptr_t address);
void tlbirpalos_16k(uintptr_t address);
void tlbirpalos_64k(uintptr_t address);
void tlbirpalos_2m(uintptr_t address);
void tlbirpalos_32m(uintptr_t address);
void tlbirpalos_512m(uintptr_t address);
void flush_dcache_to_popa_range(uintptr_t address, size_t size);
void flush_dcache_to_popa_range_mte2(uintptr_t address, size_t size);

#endif /* FIRME_TEST_ARCH_HELPERS_H */
