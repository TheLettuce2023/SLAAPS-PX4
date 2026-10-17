find_package(gz-transport13 REQUIRED)
find_package(gz-msgs10 REQUIRED)
set(fixture_root "${PX4_SOURCE_DIR}/Tools/research_sitl/stabilized_manual")
px4_add_module(
    MODULE modules__research_stabilized_sitl
    MAIN research_stabilized_sitl
    EXTERNAL
    SRCS ${fixture_root}/StabilizedManualSITL.cpp
    DEPENDS research_stabilized_manual gz-transport13::core gz-msgs10::core
)
target_link_libraries(modules__research_stabilized_sitl PRIVATE px4_work_queue)
target_include_directories(modules__research_stabilized_sitl PRIVATE ${fixture_root})
target_compile_definitions(modules__research_stabilized_sitl PRIVATE RESEARCH_STABILIZED_SITL_ONLY=1)
target_compile_features(modules__research_stabilized_sitl PRIVATE cxx_std_17)
