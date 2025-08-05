/*
 * Copyright (c) 2025, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*****************************************************************************
 *
 * Filename:
 * ---------
 *   tzgp_lib_ssr_rng_export.h
 *
 * Project:
 * --------
 *   SSR
 *
 * Description:
 * ------------
 *   SSR random number declaration
 *
 * Author:
 * -------
 *   Clean Room
 *
 * Lib version from:
 * -------
 *    ssr_core_isd_cl_6752
 *
 ****************************************************************************/
#ifndef SSR_LIB_RNG_H
#define SSR_LIB_RNG_H

/* unsigned int */
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;

/* signed int */
typedef signed char INT8;
typedef signed short INT16;
typedef signed int INT32;
typedef signed long long INT64;

typedef unsigned long addr_t;

#define SSR_OK (0)

//------------------------------------------------------------------------------
/// @brief The CLK enable/disable.
/// It should be completed successfully before the engine is used for any
/// subsequent processes.
/// @param[in] hw_top_base: set SSR_TOP_BASE to be hw_top_base.
/// @return SSR_OK: Process success.
//------------------------------------------------------------------------------
UINT32 SSR_RNG_EnClk(addr_t hw_top_base, bool clk_on);
//------------------------------------------------------------------------------
/// @brief The SW Reset API provides the capability to reset the system state to
/// known initial conditions.
/// @param[in] hw_rng_base: set SSR_RNG_BASE to be rng_base.
/// @return SSR_OK: Process success.
//------------------------------------------------------------------------------
UINT32 SSR_RNG_RstSw(addr_t hw_rng_base);
//------------------------------------------------------------------------------
/// @brief The Get Random Value API provides random number from noise source
/// with health check, conditioning, DRBG, and include check error and handling.
/// @param[in] hw_rng_base: set SSR_RNG_BASE to be rng_base.
/// @param[out] u32Rand: random number.
/// @return SSR_OK: Process success.
//------------------------------------------------------------------------------
UINT32 SSR_RNG_GetVal(addr_t hw_rng_base, UINT32 *u32Rand);

#endif /* SSR_LIB_RNG_H */
