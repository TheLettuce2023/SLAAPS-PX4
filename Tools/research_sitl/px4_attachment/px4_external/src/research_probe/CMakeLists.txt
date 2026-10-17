if(NOT PX4_PLATFORM STREQUAL "posix" OR NOT PX4_BOARD STREQUAL "px4_sitl")
	message(FATAL_ERROR "C1A research_probe is restricted to POSIX PX4 SITL")
endif()

if(ENABLE_LOCKSTEP_SCHEDULER)
	message(FATAL_ERROR "C1A research_probe requires BOARD_NOLOCKSTEP")
endif()

px4_add_module(
	MODULE modules__research_probe
	MAIN research_probe
	EXTERNAL
	SRCS research_probe.cpp
)
