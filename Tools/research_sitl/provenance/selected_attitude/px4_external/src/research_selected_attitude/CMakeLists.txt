set(selected_attitude_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selected_attitude")
px4_add_module(
    MODULE modules__research_selected_attitude
    MAIN research_selected_attitude
    EXTERNAL
    SRCS ${selected_attitude_root}/SelectedAttitude.cpp
)
target_include_directories(modules__research_selected_attitude PRIVATE ${selected_attitude_root})
target_compile_definitions(modules__research_selected_attitude PRIVATE
    RESEARCH_P2G_SELECTOR_STATE=1
    RESEARCH_P2G_MULTI_SELECTOR=1
    RESEARCH_P2H_SELECTOR_DISPOSITION=1
    RESEARCH_P2I_SELECTED_ATTITUDE=1)
target_compile_options(modules__research_selected_attitude PRIVATE -fstack-usage)
target_compile_features(modules__research_selected_attitude PRIVATE cxx_std_17)
