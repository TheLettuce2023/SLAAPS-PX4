px4_add_module(
	MODULE modules__research_state_capture
	MAIN research_state_capture
	EXTERNAL
	SRCS
		research_state_capture.cpp
		../../../producer/StateCaptureProducer.cpp
		../../../producer/Px4StateReceiptProducer.cpp
)
target_compile_definitions(modules__research_state_capture PRIVATE RESEARCH_C1C_CAPTURE=1)
target_compile_features(modules__research_state_capture PRIVATE cxx_std_17)
target_include_directories(modules__research_state_capture PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../../producer")
