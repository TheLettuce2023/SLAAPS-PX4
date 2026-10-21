cmake_minimum_required(VERSION 3.16)
project(stabilized_session_transport LANGUAGES CXX)
include(CTest)
find_package(gz-msgs10 REQUIRED)
get_filename_component(PX4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PX4_ROOT}/Tools/msg/px_generate_uorb_topic_files.py"
    --headers -f "${PX4_ROOT}/msg/RateCtrlStatus.msg" -e "${PX4_ROOT}/Tools/msg/templates/uorb"
    -o "${CMAKE_CURRENT_BINARY_DIR}/generated/uORB/topics" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "RateCtrlStatus generation failed")
endif()
add_executable(transport_test TransportTest.cpp)
target_compile_definitions(transport_test PRIVATE __EXPORT=)
target_compile_features(transport_test PRIVATE cxx_std_17)
target_include_directories(transport_test PRIVATE
    "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib" "${PX4_ROOT}/src/lib/matrix"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include")
target_compile_options(transport_test PRIVATE -Wall -Wextra -Werror -Wpedantic -Wdouble-promotion)
target_link_libraries(transport_test PRIVATE gz-msgs10::core)
add_test(NAME transport_test COMMAND transport_test)

add_executable(plant_endpoint_test PlantEndpointTest.cpp
    "${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedOutput.cpp"
    "${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedOutputDriver.cpp")
target_compile_features(plant_endpoint_test PRIVATE cxx_std_17)
target_compile_definitions(plant_endpoint_test PRIVATE __EXPORT=)
target_include_directories(plant_endpoint_test PRIVATE
    "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib" "${PX4_ROOT}/src/lib/matrix"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include")
target_compile_options(plant_endpoint_test PRIVATE -Wall -Wextra -Werror -Wpedantic -Wdouble-promotion)
target_link_libraries(plant_endpoint_test PRIVATE gz-msgs10::core)
add_test(NAME plant_endpoint_test COMMAND plant_endpoint_test)

option(BUILD_SESSION_PLUGIN "Build isolated session-aware Gazebo plant" OFF)
if(BUILD_SESSION_PLUGIN)
    find_package(gz-sim8 REQUIRED)
    find_package(gz-transport13 REQUIRED)
    add_executable(plugin_probe PluginProbe.cpp)
    target_compile_features(plugin_probe PRIVATE cxx_std_17)
    target_compile_definitions(plugin_probe PRIVATE __EXPORT=)
    target_include_directories(plugin_probe PRIVATE "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib" "${PX4_ROOT}/src/lib/matrix"
        "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include")
    target_compile_options(plugin_probe PRIVATE -Wall -Wextra -Werror -Wpedantic)
    target_link_libraries(plugin_probe PRIVATE gz-transport13::core gz-msgs10::core)
    find_package(gz-plugin2 REQUIRED COMPONENTS register)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/make_plugin.py"
        "${PX4_ROOT}/Tools/research_sitl/stabilized_manual/RotationalFixture.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/RotationalSessionFixture.cpp" RESULT_VARIABLE transformed)
    if(NOT transformed EQUAL 0)
        message(FATAL_ERROR "Private plugin transformation failed")
    endif()
    add_library(research_rotational_session SHARED "${CMAKE_CURRENT_BINARY_DIR}/RotationalSessionFixture.cpp"
        "${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedOutput.cpp"
        "${PX4_ROOT}/src/lib/research_stabilized_manual/StabilizedOutputDriver.cpp")
    target_compile_features(research_rotational_session PRIVATE cxx_std_17)
    target_compile_definitions(research_rotational_session PRIVATE __EXPORT=)
    target_include_directories(research_rotational_session PRIVATE "${CMAKE_CURRENT_LIST_DIR}"
        "${PX4_ROOT}/Tools/research_sitl/stabilized_manual" "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib"
        "${PX4_ROOT}/src/lib/matrix" "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include")
    target_link_libraries(research_rotational_session PRIVATE gz-sim8::gz-sim8 gz-plugin2::register)
    target_compile_options(research_rotational_session PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_executable(remote_completion_test RemoteCompletionTest.cpp)
target_compile_features(remote_completion_test PRIVATE cxx_std_17)
target_compile_definitions(remote_completion_test PRIVATE __EXPORT=)
target_include_directories(remote_completion_test PRIVATE
    "${PX4_ROOT}/src" "${PX4_ROOT}/src/lib" "${PX4_ROOT}/src/lib/matrix"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PX4_ROOT}/platforms/common" "${PX4_ROOT}/platforms/ros2/include")
target_compile_options(remote_completion_test PRIVATE -Wall -Wextra -Werror -Wpedantic -Wdouble-promotion)
target_link_libraries(remote_completion_test PRIVATE gz-msgs10::core)
add_test(NAME remote_completion_test COMMAND remote_completion_test)
