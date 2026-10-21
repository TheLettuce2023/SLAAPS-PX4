find_package(gz-transport13 REQUIRED)
find_package(gz-msgs10 REQUIRED)
set(session_root "${PX4_SOURCE_DIR}/Tools/research_sitl/stabilized_session")
px4_add_module(
    MODULE modules__research_session_consumer
    MAIN research_session_consumer
    EXTERNAL
    SRCS ${session_root}/Px4SessionConsumer.cpp
    DEPENDS research_stabilized_manual gz-transport13::core gz-msgs10::core
)
target_link_libraries(modules__research_session_consumer PRIVATE px4_work_queue)
target_include_directories(modules__research_session_consumer PRIVATE ${session_root})
target_compile_definitions(modules__research_session_consumer PRIVATE RESEARCH_STABILIZED_SITL_ONLY=1)
target_compile_features(modules__research_session_consumer PRIVATE cxx_std_17)

px4_add_module(
    MODULE modules__research_session_sitl
    MAIN research_session_sitl
    EXTERNAL
    SRCS ${session_root}/Px4SessionSITL.cpp
    DEPENDS research_stabilized_manual gz-transport13::core gz-msgs10::core
)
target_link_libraries(modules__research_session_sitl PRIVATE px4_work_queue)
target_include_directories(modules__research_session_sitl PRIVATE ${session_root})
target_compile_definitions(modules__research_session_sitl PRIVATE RESEARCH_STABILIZED_SITL_ONLY=1)
target_compile_features(modules__research_session_sitl PRIVATE cxx_std_17)
