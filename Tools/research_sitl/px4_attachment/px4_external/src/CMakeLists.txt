# C1A is an explicit external-module selection, never part of the stock board.
set(config_module_list_external research_probe PARENT_SCOPE)

# Commander references these parameters even in the passive minimal build.
# Register existing metadata only; do not enable Navigator or sensor modules.
set_property(GLOBAL APPEND PROPERTY PX4_MODULE_CONFIG_FILES
	"${PX4_SOURCE_DIR}/src/modules/navigator/geofence_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/sensor_params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/sensors/vehicle_gps_position/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/rc_update/params.yaml"
	"${PX4_SOURCE_DIR}/src/modules/logger/module.yaml"
	"${PX4_SOURCE_DIR}/src/modules/navigator/rtl_params.yaml"
)
