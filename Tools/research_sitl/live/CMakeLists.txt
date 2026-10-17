cmake_minimum_required(VERSION 3.16)
project(research_c1h_host LANGUAGES CXX)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "C1H is a standalone host experiment, never a PX4 module")
endif()
if(NOT EXISTS "${C1H_GENERATED_C_DIRECTORY}/research/mavlink.h")
    message(FATAL_ERROR "Supply the pinned, validated private generated dialect")
endif()
get_filename_component(RESEARCH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(RESEARCH_LIB "${RESEARCH_ROOT}/src/lib/research_acceleration")
set(SUPERVISOR "${CMAKE_CURRENT_SOURCE_DIR}/../supervisor")
add_library(research_c1h SHARED
    HostOwner.cpp SignedMavlink.cpp
    "${SUPERVISOR}/HarnessSupervisorOwner.cpp"
    "${SUPERVISOR}/SupervisorBroker.cpp"
    "${RESEARCH_LIB}/RequestGuard.cpp"
    "${RESEARCH_LIB}/FrameMath.cpp"
    "${RESEARCH_LIB}/TrustedTimeMap.cpp"
    "${RESEARCH_LIB}/WireAdapter.cpp"
    "${RESEARCH_LIB}/WireStatus.cpp")
target_compile_features(research_c1h PUBLIC cxx_std_17)
target_compile_options(research_c1h PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(research_c1h PUBLIC
    "${RESEARCH_ROOT}/src" "${RESEARCH_LIB}" "${RESEARCH_LIB}/.."
    "${RESEARCH_ROOT}/platforms/ros2/include")
target_include_directories(research_c1h SYSTEM PUBLIC "${C1H_GENERATED_C_DIRECTORY}")
enable_testing()
add_executable(signed_mavlink_test SignedMavlinkTest.cpp)
target_compile_options(signed_mavlink_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(signed_mavlink_test PRIVATE research_c1h)
add_test(NAME signed_mavlink_test COMMAND signed_mavlink_test)
add_executable(host_owner_test HostOwnerTest.cpp)
target_compile_options(host_owner_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(host_owner_test PRIVATE research_c1h)
add_test(NAME host_owner_test COMMAND host_owner_test)
