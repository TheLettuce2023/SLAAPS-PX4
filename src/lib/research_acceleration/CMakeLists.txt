cmake_minimum_required(VERSION 3.16)

project(research_acceleration LANGUAGES CXX)

# This build entry is for host-only contract tests, not PX4 module integration.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "Use this directory as a standalone host-test project")
endif()

include(CTest)

option(RESEARCH_SITL_DIAGNOSTIC_TESTS "Build isolated offline diagnostic guard and adapter tests" OFF)
option(RESEARCH_SITL_NATIVE_OWNER_TESTS "Build synthetic native-contract closed-owner tests" OFF)
option(RESEARCH_SITL_NATIVE_INGRESS_TESTS "Build synthetic C1E negative-ingress owner tests" OFF)

if(RESEARCH_SITL_DIAGNOSTIC_TESTS OR RESEARCH_SITL_NATIVE_OWNER_TESTS OR RESEARCH_SITL_NATIVE_INGRESS_TESTS)
	if(NOT BUILD_TESTING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING
		OR DEFINED PX4_PLATFORM OR DEFINED CONFIG_BOARD OR DEFINED BOARD
		OR DEFINED ENABLE_LOCKSTEP_SCHEDULER OR DEFINED PX4_SIMULATOR_TIMESTAMP_PROXY)
		message(FATAL_ERROR "Diagnostic validation requires standalone native Linux tests, not PX4/hardware/lockstep/proxy")
	endif()
endif()

add_library(research_acceleration STATIC
	RequestGuard.cpp
	FrameMath.cpp
	TrustedTimeMap.cpp
	WireAdapter.cpp
	WireStatus.cpp
)

target_include_directories(research_acceleration PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_CURRENT_LIST_DIR}/..
	# Matrix transitively includes defines.h. This existing minimal header has
	# constants/finite predicates only; no ROS middleware or PX4 runtime is linked.
	${CMAKE_CURRENT_LIST_DIR}/../../../platforms/ros2/include
)
target_compile_features(research_acceleration PUBLIC cxx_std_17)

if(BUILD_TESTING)
	# Cooperative SITL operator broker: host-test target only, never firmware.
	set(SUPERVISOR_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../Tools/research_sitl/supervisor)
	add_library(research_sitl_supervisor STATIC ${SUPERVISOR_SOURCE_DIR}/SupervisorBroker.cpp)
	target_include_directories(research_sitl_supervisor PUBLIC ${SUPERVISOR_SOURCE_DIR})
	target_link_libraries(research_sitl_supervisor PUBLIC research_acceleration)
	add_executable(research_sitl_supervisor_test ${SUPERVISOR_SOURCE_DIR}/SupervisorBrokerTest.cpp)
	target_link_libraries(research_sitl_supervisor_test PRIVATE research_sitl_supervisor)
	add_test(NAME research_sitl_supervisor_test COMMAND research_sitl_supervisor_test)
	add_executable(research_sitl_supervisor_matrix_test ${SUPERVISOR_SOURCE_DIR}/SupervisorBrokerMatrixTest.cpp)
	target_link_libraries(research_sitl_supervisor_matrix_test PRIVATE research_sitl_supervisor)
	add_test(NAME research_sitl_supervisor_matrix_test COMMAND research_sitl_supervisor_matrix_test)
	add_library(research_sitl_supervisor_owner STATIC ${SUPERVISOR_SOURCE_DIR}/HarnessSupervisorOwner.cpp)
	target_link_libraries(research_sitl_supervisor_owner PUBLIC research_sitl_supervisor)
	add_executable(research_sitl_supervisor_owner_test ${SUPERVISOR_SOURCE_DIR}/HarnessSupervisorOwnerTest.cpp)
	target_link_libraries(research_sitl_supervisor_owner_test PRIVATE research_sitl_supervisor_owner)
	add_test(NAME research_sitl_supervisor_owner_test COMMAND research_sitl_supervisor_owner_test)

	add_executable(research_acceleration_request_guard_test RequestGuardTest.cpp)
	target_link_libraries(research_acceleration_request_guard_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_request_guard_test COMMAND research_acceleration_request_guard_test)

	add_executable(research_acceleration_frame_math_test FrameMathTest.cpp)
	target_link_libraries(research_acceleration_frame_math_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_frame_math_test COMMAND research_acceleration_frame_math_test)

	add_executable(research_acceleration_trusted_time_map_test TrustedTimeMapTest.cpp)
	target_link_libraries(research_acceleration_trusted_time_map_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_trusted_time_map_test COMMAND research_acceleration_trusted_time_map_test)

	add_executable(research_acceleration_wire_adapter_state_test WireAdapterStateTest.cpp)
	target_link_libraries(research_acceleration_wire_adapter_state_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_wire_adapter_state_test COMMAND research_acceleration_wire_adapter_state_test)

	add_executable(research_acceleration_wire_adapter_session_test WireAdapterSessionTest.cpp)
	target_link_libraries(research_acceleration_wire_adapter_session_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_wire_adapter_session_test COMMAND research_acceleration_wire_adapter_session_test)

	add_executable(research_acceleration_wire_adapter_admission_test WireAdapterAdmissionTest.cpp)
	target_link_libraries(research_acceleration_wire_adapter_admission_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_wire_adapter_admission_test COMMAND research_acceleration_wire_adapter_admission_test)

	add_executable(research_acceleration_wire_status_test WireStatusTest.cpp)
	target_link_libraries(research_acceleration_wire_status_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_wire_status_test COMMAND research_acceleration_wire_status_test)
	add_executable(research_acceleration_wire_status_matrix_test WireStatusMatrixTest.cpp)
	target_link_libraries(research_acceleration_wire_status_matrix_test PRIVATE research_acceleration)
	add_test(NAME research_acceleration_wire_status_matrix_test COMMAND research_acceleration_wire_status_matrix_test)

	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	if(RESEARCH_SITL_NATIVE_INGRESS_TESTS)
		# Distinct synthetic C1E class layout; never linked to Flight/C1D/offline objects.
		add_library(research_acceleration_ingress_contract STATIC
			RequestGuard.cpp DiagnosticGuard.cpp DiagnosticAdapter.cpp FrameMath.cpp
			TrustedTimeMap.cpp WireAdapter.cpp NativeIngressDiagnosticOwner.cpp)
		target_include_directories(research_acceleration_ingress_contract PUBLIC
			${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..
			${CMAKE_CURRENT_LIST_DIR}/../../../platforms/ros2/include)
		target_compile_features(research_acceleration_ingress_contract PUBLIC cxx_std_17)
		target_compile_definitions(research_acceleration_ingress_contract PUBLIC
			RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1 RESEARCH_C1E_NEGATIVE_CONTRACT_TEST=1)
		target_compile_options(research_acceleration_ingress_contract PRIVATE -Wall -Wextra -Wpedantic -Werror -fstack-usage)
		add_executable(research_sitl_native_ingress_owner_test NativeIngressDiagnosticOwnerTest.cpp)
		target_link_libraries(research_sitl_native_ingress_owner_test PRIVATE research_acceleration_ingress_contract)
		target_compile_options(research_sitl_native_ingress_owner_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_test(NAME research_sitl_native_ingress_owner_test COMMAND research_sitl_native_ingress_owner_test)
		add_test(NAME research_sitl_native_ingress_build_isolation
			COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_LIST_DIR}/NativeIngressBuildIsolationTest.py
			--compiler ${CMAKE_CXX_COMPILER} --source-dir ${CMAKE_CURRENT_LIST_DIR})
	endif()
	if(RESEARCH_SITL_NATIVE_OWNER_TESTS)
		# Synthetic native-contract host objects, never the actual PX4 profile.
		# Do not link this variant and the offline/ordinary variants together.
		add_library(research_acceleration_native_contract STATIC
			RequestGuard.cpp DiagnosticGuard.cpp DiagnosticAdapter.cpp FrameMath.cpp
			TrustedTimeMap.cpp WireAdapter.cpp WireStatus.cpp NativeClosedDiagnosticOwner.cpp)
		target_include_directories(research_acceleration_native_contract PUBLIC
			${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..
			${CMAKE_CURRENT_LIST_DIR}/../../../platforms/ros2/include)
		target_compile_features(research_acceleration_native_contract PUBLIC cxx_std_17)
		target_compile_definitions(research_acceleration_native_contract PUBLIC
			RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1)
		target_compile_options(research_acceleration_native_contract PRIVATE -Wall -Wextra -Wpedantic -Werror -fstack-usage)
		add_executable(research_sitl_native_closed_owner_test NativeClosedDiagnosticOwnerTest.cpp)
		target_link_libraries(research_sitl_native_closed_owner_test PRIVATE research_acceleration_native_contract)
		target_compile_options(research_sitl_native_closed_owner_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_test(NAME research_sitl_native_closed_owner_test COMMAND research_sitl_native_closed_owner_test)
		foreach(adapter_suite State Session Admission)
			add_executable(research_acceleration_native_flight_${adapter_suite} WireAdapter${adapter_suite}Test.cpp)
			target_link_libraries(research_acceleration_native_flight_${adapter_suite} PRIVATE research_acceleration_native_contract)
			add_test(NAME research_acceleration_native_flight_${adapter_suite} COMMAND research_acceleration_native_flight_${adapter_suite})
		endforeach()
		add_executable(research_acceleration_native_flight_guard RequestGuardTest.cpp)
		target_link_libraries(research_acceleration_native_flight_guard PRIVATE research_acceleration_native_contract)
		add_test(NAME research_acceleration_native_flight_guard COMMAND research_acceleration_native_flight_guard)
		add_test(NAME research_sitl_native_diagnostic_build_isolation
			COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_LIST_DIR}/NativeDiagnosticBuildIsolationTest.py
			--compiler ${CMAKE_CXX_COMPILER} --source-dir ${CMAKE_CURRENT_LIST_DIR})
	endif()
	if(RESEARCH_SITL_DIAGNOSTIC_TESTS)
		# Separate objects/definitions: no executable links both variants.
		add_library(research_acceleration_diagnostic STATIC
			RequestGuard.cpp DiagnosticGuard.cpp DiagnosticAdapter.cpp FrameMath.cpp TrustedTimeMap.cpp WireAdapter.cpp WireStatus.cpp)
		target_include_directories(research_acceleration_diagnostic PUBLIC
			${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..
			${CMAKE_CURRENT_LIST_DIR}/../../../platforms/ros2/include)
		target_compile_features(research_acceleration_diagnostic PUBLIC cxx_std_17)
		target_compile_definitions(research_acceleration_diagnostic PUBLIC
			RESEARCH_SITL_DIAGNOSTIC_OFFLINE=1 RESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1)
		target_compile_options(research_acceleration_diagnostic PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_executable(research_acceleration_diagnostic_guard_test DiagnosticGuardTest.cpp)
		target_compile_options(research_acceleration_diagnostic_guard_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		target_link_libraries(research_acceleration_diagnostic_guard_test PRIVATE research_acceleration_diagnostic)
		add_test(NAME research_acceleration_diagnostic_guard_test COMMAND research_acceleration_diagnostic_guard_test)
		add_executable(research_acceleration_diagnostic_adapter_test DiagnosticAdapterTest.cpp)
		target_compile_options(research_acceleration_diagnostic_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		target_link_libraries(research_acceleration_diagnostic_adapter_test PRIVATE research_acceleration_diagnostic)
		add_test(NAME research_acceleration_diagnostic_adapter_test COMMAND research_acceleration_diagnostic_adapter_test)
		add_library(research_sitl_diagnostic_supervisor STATIC
			${SUPERVISOR_SOURCE_DIR}/SupervisorBroker.cpp
			${SUPERVISOR_SOURCE_DIR}/DiagnosticSupervisorBroker.cpp
			${SUPERVISOR_SOURCE_DIR}/HarnessSupervisorOwner.cpp
			${SUPERVISOR_SOURCE_DIR}/DiagnosticHarnessSupervisorOwner.cpp)
		target_include_directories(research_sitl_diagnostic_supervisor PUBLIC ${SUPERVISOR_SOURCE_DIR})
		target_link_libraries(research_sitl_diagnostic_supervisor PUBLIC research_acceleration_diagnostic)
		target_compile_options(research_sitl_diagnostic_supervisor PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_executable(research_sitl_diagnostic_owner_test ${SUPERVISOR_SOURCE_DIR}/DiagnosticSupervisorOwnerTest.cpp)
		target_link_libraries(research_sitl_diagnostic_owner_test PRIVATE research_sitl_diagnostic_supervisor)
		target_compile_options(research_sitl_diagnostic_owner_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_test(NAME research_sitl_diagnostic_owner_test COMMAND research_sitl_diagnostic_owner_test)
		set(DIAGNOSTIC_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../Tools/research_sitl/diagnostic)
		find_package(Threads REQUIRED)
		add_executable(diagnostic_campaign ${DIAGNOSTIC_SOURCE_DIR}/DiagnosticCampaign.cpp ${DIAGNOSTIC_SOURCE_DIR}/DiagnosticJson.cpp)
		target_link_libraries(diagnostic_campaign PRIVATE research_sitl_diagnostic_supervisor Threads::Threads)
		target_compile_options(diagnostic_campaign PRIVATE -Wall -Wextra -Wpedantic -Werror)
		add_test(NAME research_sitl_diagnostic_campaign
			COMMAND ${Python3_EXECUTABLE} -B ${DIAGNOSTIC_SOURCE_DIR}/run_campaign.py
			--executable $<TARGET_FILE:diagnostic_campaign> --build-dir ${CMAKE_BINARY_DIR}
			--source-root ${CMAKE_CURRENT_LIST_DIR}/../../..)
		add_test(NAME research_sitl_diagnostic_evidence_corruptions
			COMMAND ${Python3_EXECUTABLE} -B ${DIAGNOSTIC_SOURCE_DIR}/test_check_campaign.py
			--executable $<TARGET_FILE:diagnostic_campaign> --build-dir ${CMAKE_BINARY_DIR}
			--source-root ${CMAKE_CURRENT_LIST_DIR}/../../..)
		add_test(NAME research_sitl_diagnostic_sink_isolation COMMAND diagnostic_campaign --sink-test)
		foreach(supervisor_suite SupervisorBroker SupervisorBrokerMatrix HarnessSupervisorOwner)
			add_executable(research_sitl_diagnostic_flight_${supervisor_suite} ${SUPERVISOR_SOURCE_DIR}/${supervisor_suite}Test.cpp)
			target_link_libraries(research_sitl_diagnostic_flight_${supervisor_suite} PRIVATE research_sitl_diagnostic_supervisor)
			add_test(NAME research_sitl_diagnostic_flight_${supervisor_suite} COMMAND research_sitl_diagnostic_flight_${supervisor_suite})
		endforeach()
		foreach(adapter_suite State Session Admission)
			add_executable(research_acceleration_diagnostic_flight_${adapter_suite} WireAdapter${adapter_suite}Test.cpp)
			target_link_libraries(research_acceleration_diagnostic_flight_${adapter_suite} PRIVATE research_acceleration_diagnostic)
			add_test(NAME research_acceleration_diagnostic_flight_${adapter_suite} COMMAND research_acceleration_diagnostic_flight_${adapter_suite})
		endforeach()
		# Also exercise ordinary Flight construction in the opted-in class layout.
		add_executable(research_acceleration_diagnostic_flight_regression RequestGuardTest.cpp)
		target_link_libraries(research_acceleration_diagnostic_flight_regression PRIVATE research_acceleration_diagnostic)
		add_test(NAME research_acceleration_diagnostic_flight_regression COMMAND research_acceleration_diagnostic_flight_regression)
		add_test(NAME research_acceleration_diagnostic_build_isolation
			COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_LIST_DIR}/DiagnosticBuildIsolationTest.py
			--compiler ${CMAKE_CXX_COMPILER} --source-dir ${CMAKE_CURRENT_LIST_DIR})
	endif()
	set(PROFILE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../Tools/research_sitl/profile)
	add_executable(research_sitl_timing_profile_test ${PROFILE_SOURCE_DIR}/TimingProfileTest.cpp)
	target_link_libraries(research_sitl_timing_profile_test PRIVATE research_acceleration)
	add_test(NAME research_sitl_timing_profile_test COMMAND research_sitl_timing_profile_test)
	add_test(NAME research_sitl_timing_profile_evidence
		COMMAND ${Python3_EXECUTABLE} -B ${PROFILE_SOURCE_DIR}/check_timing_profile.py
		$<TARGET_FILE:research_sitl_timing_profile_test>)
	add_executable(research_sitl_status_delivery_test ${PROFILE_SOURCE_DIR}/StatusDeliveryTest.cpp)
	target_link_libraries(research_sitl_status_delivery_test PRIVATE research_acceleration)
	add_test(NAME research_sitl_status_delivery_test COMMAND research_sitl_status_delivery_test)
	add_test(NAME research_sitl_status_delivery_evidence
		COMMAND ${Python3_EXECUTABLE} -B ${PROFILE_SOURCE_DIR}/check_status_delivery.py
		$<TARGET_FILE:research_sitl_status_delivery_test>)
	add_test(NAME research_sitl_policy_contract_test
		COMMAND ${Python3_EXECUTABLE} -B ${PROFILE_SOURCE_DIR}/test_policy_contract.py)
	add_test(NAME research_acceleration_wire_status_vectors
		COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_LIST_DIR}/../../../Tools/research_sitl/wire/test_status_vectors.py
		$<TARGET_FILE:research_acceleration_wire_status_test>)
endif()
