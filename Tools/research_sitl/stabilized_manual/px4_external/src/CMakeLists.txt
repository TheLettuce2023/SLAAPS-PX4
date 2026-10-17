include(px4_add_library)
add_subdirectory("${PX4_SOURCE_DIR}/src/lib/research_stabilized_manual"
    "${CMAKE_CURRENT_BINARY_DIR}/research_stabilized_manual")
add_subdirectory(research_stabilized_sitl)
