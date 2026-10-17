if(NOT RESEARCH_C1C_CAPTURE OR NOT PX4_PLATFORM STREQUAL "posix" OR NOT PX4_BOARD STREQUAL "px4_sitl")
	message(FATAL_ERROR "C1C requires explicit POSIX PX4 SITL capture configuration")
endif()
if(NOT PX4_CONFIG_FILE MATCHES "/c1c-default[.]px4board$" OR ENABLE_LOCKSTEP_SCHEDULER)
	message(FATAL_ERROR "C1C requires its dedicated non-lockstep board configuration")
endif()
set(config_module_list_external research_state_capture PARENT_SCOPE)

# Metadata dependencies only; no owning runtime module is selected.
set_property(GLOBAL APPEND PROPERTY PX4_MODULE_CONFIG_FILES
	"${PX4_SOURCE_DIR}/src/modules/navigator/geofence_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/rc_update/params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/logger/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/navigator/rtl_params.yaml"
)
