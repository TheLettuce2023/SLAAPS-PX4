# Private POSIX-only verification overlay; never registered in checkout src/.
px4_add_module(
	MODULE examples__bench_active_test
	MAIN bench_active_test
	SRCS BenchActiveTest.cpp
	MODULE_CONFIG test_params.yaml
	DEPENDS mixer_module modules__manual_control modules__research_bench_control
)
# Do not propagate mavlink_c INTERFACE_SOURCES into this directory before its
# generated headers exist on a fresh configure. Depend on generation explicitly.
add_dependencies(examples__bench_active_test mavlink_c_generate)
target_include_directories(examples__bench_active_test SYSTEM PRIVATE
	"${PX4_BINARY_DIR}/mavlink/${CONFIG_MAVLINK_DIALECT}")
