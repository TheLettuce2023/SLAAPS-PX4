cmake_minimum_required(VERSION 3.16)
project(slaaps_configuration_tests LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(PX4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics")
execute_process(
	COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/Tools/msg/px_generate_uorb_topic_files.py"
		--headers -f "${PX4_ROOT}/msg/RateCtrlStatus.msg"
		"${PX4_ROOT}/msg/SlaapsOutputBank.msg"
		"${PX4_ROOT}/msg/SlaapsManualCommand.msg"
		"${PX4_ROOT}/msg/SlaapsManualAck.msg"
		"${PX4_ROOT}/msg/SlaapsManualPreparation.msg"
		-e "${PX4_ROOT}/Tools/msg/templates/uorb"
		-o "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics"
	RESULT_VARIABLE GENERATION_RESULT
)
if(NOT GENERATION_RESULT EQUAL 0)
	message(FATAL_ERROR "PX4 message generation failed")
endif()
execute_process(
	COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/src/lib/mixer_module/generate_function_header.py"
		"${PX4_ROOT}/src/lib/mixer_module/output_functions.yaml"
		"${CMAKE_CURRENT_BINARY_DIR}/generated/output_functions.hpp"
	RESULT_VARIABLE GENERATION_RESULT
)
if(NOT GENERATION_RESULT EQUAL 0)
	message(FATAL_ERROR "PX4 output function generation failed")
endif()

add_executable(slaaps_configuration_test
	VehicleConfigurationTest.cpp
	"${PX4_ROOT}/src/lib/slaaps/VehicleConfiguration.cpp"
	"${PX4_ROOT}/src/lib/slaaps/ActuatorRouting.cpp"
	"${PX4_ROOT}/src/lib/slaaps/EdfOwner.cpp"
	"${PX4_ROOT}/src/lib/slaaps/ReportedRouting.cpp"
	"${PX4_ROOT}/src/lib/research_stabilized_manual/CanardAllocator.cpp"
	"${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedManualControl.cpp"
	"${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedInputValidation.cpp"
	"${PX4_ROOT}/src/lib/rate_control/rate_control.cpp"
	"${PX4_ROOT}/src/lib/research_actuator_command/ManualControlInput.cpp"
	"${PX4_ROOT}/src/lib/research_actuator_command/CommandLimiter.cpp"
	"${PX4_ROOT}/src/lib/research_actuator_command/BenchControlOwner.cpp"
	"${PX4_ROOT}/src/lib/research_actuator_command/BenchOutputLease.cpp"
)
target_compile_features(slaaps_configuration_test PRIVATE cxx_std_17)
target_compile_definitions(slaaps_configuration_test PRIVATE __EXPORT=)
target_include_directories(slaaps_configuration_test PRIVATE
	"${PX4_ROOT}/src" "${PX4_ROOT}/src/lib" "${PX4_ROOT}/src/lib/matrix"
	"${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include"
	"${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_options(slaaps_configuration_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
option(SLAAPS_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(SLAAPS_SANITIZERS)
	target_compile_options(slaaps_configuration_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(slaaps_configuration_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME slaaps_configuration COMMAND slaaps_configuration_test)
add_test(NAME slaaps_parameter_metadata
	COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/Tools/slaaps/generate_parameters.py" --check)
