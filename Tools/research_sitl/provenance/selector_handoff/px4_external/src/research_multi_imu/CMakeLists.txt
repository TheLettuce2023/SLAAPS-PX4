set(selector_handoff_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_handoff")
set(selected_attitude_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selected_attitude")
file(RELATIVE_PATH gps_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml")
file(RELATIVE_PATH sensor_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params.yaml")
file(RELATIVE_PATH magnetometer_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params_mag.yaml")
px4_add_module(
    MODULE modules__research_multi_imu
    MAIN research_multi_imu
    EXTERNAL
    SRCS
        ${selector_handoff_root}/research_multi_imu.cpp
        ${selected_attitude_root}/RecordedSystemState.cpp
    MODULE_CONFIG
        ${gps_parameter_config}
        ${sensor_parameter_config}
        ${magnetometer_parameter_config}
)
target_include_directories(modules__research_multi_imu PRIVATE
    ${selector_handoff_root}
    ${selected_attitude_root})
target_compile_definitions(modules__research_multi_imu PRIVATE
    RESEARCH_P2G_SELECTOR_STATE=1
    RESEARCH_P2G_MULTI_SELECTOR=1
    RESEARCH_P2H_SELECTOR_DISPOSITION=1
    RESEARCH_P2I_SELECTED_ATTITUDE=1
    RESEARCH_P2J_SELECTOR_HANDOFF=1)
target_compile_options(modules__research_multi_imu PRIVATE -fstack-usage)
target_compile_features(modules__research_multi_imu PRIVATE cxx_std_17)
