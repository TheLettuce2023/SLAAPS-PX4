if(NOT RESEARCH_C1E_NEGATIVE_OWNER OR NOT PX4_PLATFORM STREQUAL "posix" OR NOT PX4_BOARD STREQUAL "px4_sitl")
	message(FATAL_ERROR "C1E requires explicit native Linux POSIX SITL negative-owner configuration")
endif()
if(NOT PX4_CONFIG_FILE MATCHES "/c1e-default[.]px4board$" OR ENABLE_LOCKSTEP_SCHEDULER)
	message(FATAL_ERROR "C1E requires its dedicated non-lockstep board configuration")
endif()
set(config_module_list_external research_native_ingress PARENT_SCOPE)

# Parameter metadata only; none of these runtime modules is selected.
set_property(GLOBAL APPEND PROPERTY PX4_MODULE_CONFIG_FILES
	"${PX4_SOURCE_DIR}/src/modules/navigator/geofence_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/rc_update/params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/logger/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/navigator/rtl_params.yaml"
)
