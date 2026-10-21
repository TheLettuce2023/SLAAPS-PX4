px4_add_module(
	MODULE modules__slaaps
	MAIN slaaps
	SRCS Slaaps.cpp Px4ConfigurationSource.cpp
	MODULE_CONFIG module.yaml
	DEPENDS slaaps_configuration px4_work_queue
)

if(CONFIG_SLAAPS_ROUTING_TEST)
	px4_add_module(
		MODULE modules__slaaps_routing_test
		MAIN slaaps_routing_test
		SRCS SlaapsRoutingTest.cpp
	)
	px4_add_module(
		MODULE modules__slaaps_control_test
		MAIN slaaps_control_test
		SRCS ${PX4_SOURCE_DIR}/Tools/slaaps/configuration/VehicleConfigurationTest.cpp
		DEPENDS slaaps_configuration output_functions_header
		INCLUDES ${CMAKE_BINARY_DIR}/src/lib/mixer_module
	)
	target_compile_definitions(modules__slaaps_control_test PRIVATE SLAAPS_NATIVE_CONTROL_TEST)
endif()
