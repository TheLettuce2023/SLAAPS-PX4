file(RELATIVE_PATH gps_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml")
px4_add_module(
    MODULE modules__research_provenance
    MAIN research_provenance
    EXTERNAL
    SRCS research_provenance.cpp
    MODULE_CONFIG
        ${gps_parameter_config}
)
target_compile_definitions(modules__research_provenance PRIVATE RESEARCH_PROVENANCE_CAPTURE=1)
target_compile_options(modules__research_provenance PRIVATE -fstack-usage)
target_compile_features(modules__research_provenance PRIVATE cxx_std_17)
