set(selector_handoff_root "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_handoff")
px4_add_module(
    MODULE modules__research_selector_handoff
    MAIN research_selector_handoff
    EXTERNAL
    SRCS ${selector_handoff_root}/SelectorHandoff.cpp
)
target_include_directories(modules__research_selector_handoff PRIVATE ${selector_handoff_root})
target_compile_definitions(modules__research_selector_handoff PRIVATE
    RESEARCH_P2J_SELECTOR_HANDOFF=1)
target_compile_options(modules__research_selector_handoff PRIVATE -fstack-usage)
target_compile_features(modules__research_selector_handoff PRIVATE cxx_std_17)
