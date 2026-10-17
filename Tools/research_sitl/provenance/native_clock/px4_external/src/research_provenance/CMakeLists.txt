set(provenance_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance")
file(RELATIVE_PATH gps_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml")
px4_add_module(
    MODULE modules__research_provenance
    MAIN research_provenance
    EXTERNAL
    SRCS ${provenance_root}/native/px4_external/src/research_provenance/research_provenance.cpp
        ${provenance_root}/native_merge/NativeMerge.cpp
        ${provenance_root}/merge/ReceiptMerge.cpp
        ${provenance_root}/provider/CheckedProvider.cpp
        ${provenance_root}/native_clock/NativeClock.cpp
    MODULE_CONFIG ${gps_parameter_config}
)
target_include_directories(modules__research_provenance PRIVATE
    ${provenance_root}/native_merge ${provenance_root}/merge ${provenance_root}/provider ${provenance_root}/native_clock)
target_compile_definitions(modules__research_provenance PRIVATE
    RESEARCH_PROVENANCE_CAPTURE=1 RESEARCH_P2D_NATIVE=1 RESEARCH_P3C_NATIVE=1)
target_compile_options(modules__research_provenance PRIVATE -fstack-usage)
target_compile_features(modules__research_provenance PRIVATE cxx_std_17)
