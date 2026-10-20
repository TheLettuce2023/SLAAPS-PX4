px4_add_module(
	MODULE examples__stabilized_observer_test
	MAIN stabilized_observer_test
	SRCS ObserverIntegrationTest.cpp
	DEPENDS px4_work_queue
)
