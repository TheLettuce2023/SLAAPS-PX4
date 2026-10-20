if(CONFIG_MODULES_RESEARCH_BENCH_CONTROL)
	message(FATAL_ERROR "Stabilized observer and research bench owner require separate images")
endif()

px4_add_module(
	MODULE modules__research_stabilized_manual
	MAIN research_stabilized_manual
	SRCS ResearchStabilizedManual.cpp
	DEPENDS research_stabilized_manual research_actuator_command px4_work_queue
)
