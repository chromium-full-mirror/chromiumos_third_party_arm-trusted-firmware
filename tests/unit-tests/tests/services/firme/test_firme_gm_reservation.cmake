#
# Copyright (c) 2026, Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

include(UnitTest)

configure_file(
	${PROJECT_SOURCE_DIR}/mocks/include/lib/libc/cdefs.h
	${CMAKE_CURRENT_BINARY_DIR}/reservation_include/cdefs.h
	COPYONLY
)

unit_test_add_suite(
	NAME firme_gm_reservation
	SOURCES
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_reservation.c
		${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_reservation.cpp
		${PROJECT_SOURCE_DIR}/mocks/common/debug.cpp
	INCLUDE_DIRECTORIES
		${PROJECT_SOURCE_DIR}/mocks/include
		${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
		${UNIT_TEST_PROJECT_PATH}/include
		${CMAKE_CURRENT_BINARY_DIR}/reservation_include
	COMPILE_DEFINITIONS
		LOG_LEVEL=0
		FIRME_GM_RESERVATIONS_MAX=64
		FIRME_GM_RESERVATION_CAPACITY_ONLY=0
)

# Repeat only the capacity cases for the minimum and a partial bitmap.
foreach(slot_count IN ITEMS 1 3)
	unit_test_add_suite(
		NAME firme_gm_reservation_capacity_${slot_count}
		SOURCES
			${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme/firme_gm_reservation.c
			${CMAKE_CURRENT_LIST_DIR}/test_firme_gm_reservation.cpp
			${PROJECT_SOURCE_DIR}/mocks/common/debug.cpp
		INCLUDE_DIRECTORIES
			${PROJECT_SOURCE_DIR}/mocks/include
			${UNIT_TEST_PROJECT_PATH}/services/std_svc/firme
			${UNIT_TEST_PROJECT_PATH}/include
			${CMAKE_CURRENT_BINARY_DIR}/reservation_include
		COMPILE_DEFINITIONS
			LOG_LEVEL=0
			FIRME_GM_RESERVATIONS_MAX=${slot_count}
			FIRME_GM_RESERVATION_CAPACITY_ONLY=1
	)
endforeach()
