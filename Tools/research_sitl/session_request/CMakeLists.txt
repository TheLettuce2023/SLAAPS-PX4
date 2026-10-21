cmake_minimum_required(VERSION 3.16)
project(session_request_owner LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(PX4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/Tools/msg/px_generate_uorb_topic_files.py"
    --headers -f "${PX4_ROOT}/msg/StabilizedGrantRequest.msg" "${PX4_ROOT}/msg/StabilizedGrantResponse.msg"
    -e "${PX4_ROOT}/Tools/msg/templates/uorb" -o "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics"
    RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "Grant schema generation failed")
endif()
add_executable(session_request_test SessionRequestTest.cpp)
target_compile_features(session_request_test PRIVATE cxx_std_17)
target_compile_definitions(session_request_test PRIVATE __EXPORT=)
target_include_directories(session_request_test PRIVATE "${PX4_ROOT}/src" "${PX4_ROOT}/platforms/common"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(session_request_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME session_request_test COMMAND session_request_test)
