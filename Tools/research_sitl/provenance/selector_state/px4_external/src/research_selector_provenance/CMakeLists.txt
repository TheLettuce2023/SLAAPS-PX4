set(selector_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_state")
file(RELATIVE_PATH gps_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml")
file(RELATIVE_PATH sensor_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params.yaml")
file(RELATIVE_PATH magnetometer_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params_mag.yaml")
px4_add_module(
    MODULE modules__research_selector_provenance
    MAIN research_selector_provenance
    EXTERNAL
    SRCS research_selector_provenance.cpp
        ${selector_root}/SelectorState.cpp
    MODULE_CONFIG
        ${gps_parameter_config}
        ${sensor_parameter_config}
        ${magnetometer_parameter_config}
)
target_include_directories(modules__research_selector_provenance PRIVATE ${selector_root})
target_compile_definitions(modules__research_selector_provenance PRIVATE
    RESEARCH_P2G_SELECTOR_STATE=1
    RESEARCH_P2G_MULTI_SELECTOR=1)
target_compile_options(modules__research_selector_provenance PRIVATE -fstack-usage)
target_compile_features(modules__research_selector_provenance PRIVATE cxx_std_17)
