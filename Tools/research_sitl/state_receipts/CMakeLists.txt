cmake_minimum_required(VERSION 3.16)
project(research_sitl_state_receipts LANGUAGES CXX)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "State receipt checks are standalone host tests only")
endif()

option(RESEARCH_SITL_STATE_RECEIPT_TESTS "Build synthetic state receipt tests" OFF)

if(RESEARCH_SITL_STATE_RECEIPT_TESTS)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
		message(FATAL_ERROR "State receipt checks require a native Linux host")
	endif()

	include(CTest)
	if(NOT BUILD_TESTING)
		message(FATAL_ERROR "State receipt checks require BUILD_TESTING")
	endif()
	add_library(state_receipt_checker STATIC StateReceiptChecker.cpp)
	target_compile_features(state_receipt_checker PUBLIC cxx_std_17)
	target_compile_definitions(state_receipt_checker PUBLIC RESEARCH_SITL_STATE_RECEIPT_HOST_TEST=1)
	target_compile_options(state_receipt_checker PRIVATE -Wall -Wextra -Wpedantic -Werror)
	target_include_directories(state_receipt_checker PUBLIC ${CMAKE_CURRENT_LIST_DIR})
	add_executable(state_receipt_checker_test StateReceiptCheckerTest.cpp)
	target_link_libraries(state_receipt_checker_test PRIVATE state_receipt_checker)
	target_compile_options(state_receipt_checker_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	add_test(NAME state_receipt_checker_test COMMAND state_receipt_checker_test)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	add_test(NAME state_receipt_build_isolation
		COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_LIST_DIR}/BuildIsolationTest.py
		--compiler ${CMAKE_CXX_COMPILER} --source-dir ${CMAKE_CURRENT_LIST_DIR})
endif()
