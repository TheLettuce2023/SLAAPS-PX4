px4_add_module(
	MODULE modules__research_bench_control
	MAIN research_bench_control
	SRCS ResearchBenchControl.cpp
	DEPENDS research_actuator_command px4_work_queue
)
