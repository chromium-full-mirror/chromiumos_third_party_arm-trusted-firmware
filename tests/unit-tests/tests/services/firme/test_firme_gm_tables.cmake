#
# Copyright (c) 2026, Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

include(UnitTest)

configure_file(
	${PROJECT_SOURCE_DIR}/mocks/include/lib/libc/cdefs.h
	${CMAKE_CURRENT_BINARY_DIR}/firme_tables_include/cdefs.h
	COPYONLY
)

unit_test_add_suite(
	NAME firme_gm_tables
	SOURCES
		${CMAKE_CURRENT_LIST_DIR}/firme_gm_tables.yml
		${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_tables.cpp
	INCLUDE_DIRECTORIES
		${CMAKE_CURRENT_LIST_DIR}/include
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
		${UNIT_TEST_PROJECT_PATH}/include
		${UNIT_TEST_PROJECT_PATH}/include/arch/aarch64
		${CMAKE_CURRENT_BINARY_DIR}/firme_tables_include
	COMPILE_DEFINITIONS
		ENABLE_RMM=1
)
