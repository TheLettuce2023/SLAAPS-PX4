set(disposition_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_disposition")
set(selected_attitude_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selected_attitude")
px4_add_module(
    MODULE modules__research_selector_disposition
    MAIN research_selector_disposition
    EXTERNAL
    SRCS ${disposition_root}/SelectorDisposition.cpp
)
target_include_directories(modules__research_selector_disposition PRIVATE
    ${disposition_root}
    ${selected_attitude_root})
target_compile_definitions(modules__research_selector_disposition PRIVATE
    RESEARCH_P2G_SELECTOR_STATE=1
    RESEARCH_P2G_MULTI_SELECTOR=1
    RESEARCH_P2H_SELECTOR_DISPOSITION=1
    RESEARCH_P2I_SELECTED_ATTITUDE=1)
target_compile_options(modules__research_selector_disposition PRIVATE -fstack-usage)
target_compile_features(modules__research_selector_disposition PRIVATE cxx_std_17)
