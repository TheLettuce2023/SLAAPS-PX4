set(provenance_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance")
file(RELATIVE_PATH gps_parameter_config "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml")
px4_add_module(
    MODULE modules__research_provenance
    MAIN research_provenance
    EXTERNAL
    SRCS ${provenance_root}/native/px4_external/src/research_provenance/research_provenance.cpp
        ${provenance_root}/native_merge/NativeMerge.cpp
        ${provenance_root}/committed_state/CommittedState.cpp
        ${provenance_root}/selected_state/SelectedState.cpp
        ${provenance_root}/merge/ReceiptMerge.cpp
    MODULE_CONFIG ${gps_parameter_config}
)
target_include_directories(modules__research_provenance PRIVATE
    ${provenance_root}/native_merge
    ${provenance_root}/committed_state
    ${provenance_root}/selected_state
    ${provenance_root}/merge)
target_compile_definitions(modules__research_provenance PRIVATE
    RESEARCH_PROVENANCE_CAPTURE=1
    RESEARCH_P2D_NATIVE=1
    RESEARCH_P2E_COMMITTED_STATE=1
    RESEARCH_P2F_SELECTED_STATE=1)
target_compile_options(modules__research_provenance PRIVATE -fstack-usage)
target_compile_features(modules__research_provenance PRIVATE cxx_std_17)
