cmake_minimum_required(VERSION 3.16)
project(research_stabilized_fixture LANGUAGES CXX)
find_package(gz-sim8 REQUIRED)
find_package(gz-plugin2 REQUIRED COMPONENTS register)
get_filename_component(PX4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(research_rotational_fixture SHARED RotationalFixture.cpp
    "${PX4_ROOT}/src/lib/research_actuator_command/BenchOutputLease.cpp"
    "${PX4_ROOT}/src/lib/research_actuator_command/BenchControlOwner.cpp"
    "${PX4_ROOT}/src/lib/research_actuator_command/CommandLimiter.cpp"
    "${PX4_ROOT}/src/lib/research_actuator_command/ManualControlInput.cpp")
target_compile_features(research_rotational_fixture PRIVATE cxx_std_17)
target_include_directories(research_rotational_fixture PRIVATE "${PX4_ROOT}/src")
target_link_libraries(research_rotational_fixture PRIVATE gz-sim8::gz-sim8 gz-plugin2::register)
target_compile_options(research_rotational_fixture PRIVATE -Wall -Wextra -Werror -Wpedantic)
