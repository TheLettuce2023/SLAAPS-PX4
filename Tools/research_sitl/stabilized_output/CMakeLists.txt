px4_add_module(
	MODULE examples__stabilized_output_test
	MAIN stabilized_output_test
	SRCS ScheduledOutputTest.cpp
	MODULE_CONFIG test_params.yaml
	DEPENDS research_stabilized_manual mixer_module px4_work_queue
)
