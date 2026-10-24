px4_add_module(
	MODULE modules__slaaps
	MAIN slaaps
	SRCS Slaaps.cpp
	MODULE_CONFIG module.yaml
	COMPILE_FLAGS -Wno-address-of-packed-member -Wno-cast-align
	INCLUDES ${CMAKE_BINARY_DIR}/mavlink ${CMAKE_BINARY_DIR}/mavlink/${CONFIG_MAVLINK_DIALECT}
	DEPENDS slaaps_configuration px4_work_queue mavlink_c_generate
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
		DEPENDS slaaps_configuration output_functions_header mixer_module px4_work_queue mavlink_c_generate
		COMPILE_FLAGS -Wno-address-of-packed-member -Wno-cast-align
		INCLUDES ${CMAKE_BINARY_DIR}/src/lib/mixer_module
			${CMAKE_BINARY_DIR}/mavlink ${CMAKE_BINARY_DIR}/mavlink/${CONFIG_MAVLINK_DIALECT}
	)
	# Only this isolated fixture recompiles the real supervisor with synthetic
	# authority. The normal modules__slaaps target retains explicit denial.
	target_compile_definitions(modules__slaaps_control_test PRIVATE SLAAPS_NATIVE_CONTROL_TEST SLAAPS_SUPERVISOR_TEST_ACCESS)
endif()
