cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	px4_add_library(research_stabilized_manual CanardAllocator.cpp StabilizedManualControl.cpp)
	target_link_libraries(research_stabilized_manual PRIVATE research_actuator_command RateControl)
	return()
endif()

project(research_stabilized_manual LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(PX4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics")
execute_process(
	COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/Tools/msg/px_generate_uorb_topic_files.py"
		--headers -f "${PX4_ROOT}/msg/RateCtrlStatus.msg"
		-e "${PX4_ROOT}/Tools/msg/templates/uorb"
		-o "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics"
	RESULT_VARIABLE UORB_GENERATION_RESULT
)
if(NOT UORB_GENERATION_RESULT EQUAL 0)
	message(FATAL_ERROR "Cannot generate actual PX4 RateCtrlStatus header")
endif()

add_library(research_stabilized_manual STATIC
	CanardAllocator.cpp StabilizedManualControl.cpp
	"${PX4_ROOT}/src/lib/rate_control/rate_control.cpp"
	"${PX4_ROOT}/src/lib/research_actuator_command/ManualControlInput.cpp"
)
target_compile_features(research_stabilized_manual PUBLIC cxx_std_17)
target_include_directories(research_stabilized_manual PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}" "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib"
	"${PX4_ROOT}/src/lib/matrix" "${PX4_ROOT}/platforms/common"
	# Minimal finite-predicate header only; no ROS runtime linked.
	"${PX4_ROOT}/platforms/ros2/include" "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
# Host-only export annotation; real message schema/header and uORB declarations
# are used. No fake status struct or uORB execution is introduced.
target_compile_definitions(research_stabilized_manual PUBLIC __EXPORT=)
target_compile_options(research_stabilized_manual PRIVATE
    -Wall -Wextra -Werror -Wpedantic -Wdouble-promotion -Wfloat-equal)

if(BUILD_TESTING)
	add_executable(stabilized_manual_test StabilizedManualTest.cpp)
	target_link_libraries(stabilized_manual_test PRIVATE research_stabilized_manual)
	target_compile_options(stabilized_manual_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
	add_test(NAME stabilized_manual_test COMMAND stabilized_manual_test)
endif()
