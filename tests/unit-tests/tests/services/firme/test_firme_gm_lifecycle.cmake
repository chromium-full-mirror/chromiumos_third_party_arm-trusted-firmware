#
# Copyright (c) 2026, Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

include(UnitTest)

configure_file(
	${PROJECT_SOURCE_DIR}/mocks/include/common/debug.h
	${CMAKE_CURRENT_BINARY_DIR}/lifecycle_include/common/debug.h
	COPYONLY
)

unit_test_add_suite(
	NAME firme_gm_lifecycle
	SOURCES
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_lifecycle.c
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_geometry.c
		${CMAKE_CURRENT_LIST_DIR}/firme_gm_tables.yml
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_reservation.c
		${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_lifecycle.cpp
		${PROJECT_SOURCE_DIR}/mocks/common/debug.cpp
	INCLUDE_DIRECTORIES
		${CMAKE_CURRENT_BINARY_DIR}/lifecycle_include
		${CMAKE_CURRENT_LIST_DIR}/include
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
		${UNIT_TEST_PROJECT_PATH}/include
		${UNIT_TEST_PROJECT_PATH}/include/arch/aarch64
		${PROJECT_SOURCE_DIR}/mocks/include
		${CMAKE_CURRENT_BINARY_DIR}/firme_include
	COMPILE_DEFINITIONS
		ENABLE_RMM=1
		PLAT_XLAT_TABLES_DYNAMIC=1
		FIRME_GM_RESERVATIONS_MAX=4
		LOG_LEVEL=0
)

target_link_options(firme_gm_lifecycle PRIVATE
	-Wl,--wrap=firme_gm_reserve
	-Wl,--wrap=firme_gm_release
)
