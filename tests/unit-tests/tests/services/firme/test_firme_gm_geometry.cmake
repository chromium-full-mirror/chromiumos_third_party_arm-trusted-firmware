#
# Copyright (c) 2026, Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

include(UnitTest)

# Reuse the host attribute definitions without shadowing system libc headers.
configure_file(
	${PROJECT_SOURCE_DIR}/mocks/include/lib/libc/cdefs.h
	${CMAKE_CURRENT_BINARY_DIR}/firme_include/cdefs.h
	COPYONLY
)

unit_test_add_suite(
	NAME firme_gm_geometry
	SOURCES
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_geometry.c
		${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_geometry.cpp
	INCLUDE_DIRECTORIES
		${CMAKE_CURRENT_LIST_DIR}/include
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
		${UNIT_TEST_PROJECT_PATH}/include
		${UNIT_TEST_PROJECT_PATH}/include/arch/aarch64
		${CMAKE_CURRENT_BINARY_DIR}/firme_include
)

unit_test_add_suite(
	NAME firme_gm_gpi_geometry
	SOURCES
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_geometry.c
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_gpi.c
		${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_gpi_geometry.cpp
	INCLUDE_DIRECTORIES
		${CMAKE_CURRENT_LIST_DIR}/include
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
		${UNIT_TEST_PROJECT_PATH}/include
		${UNIT_TEST_PROJECT_PATH}/include/arch/aarch64
		${CMAKE_CURRENT_BINARY_DIR}/firme_include
	COMPILE_DEFINITIONS
		FIRME_SUPPORT=1
		ENABLE_FEAT_RME=1
		ENABLE_RMM=1
		RME_GPT_MAX_BLOCK=0
		LOG_LEVEL=0
)
