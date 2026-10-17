set(c1d_core "${PX4_SOURCE_DIR}/src/lib/research_acceleration")
add_library(research_c1d_native_core STATIC
	${c1d_core}/RequestGuard.cpp
	${c1d_core}/DiagnosticGuard.cpp
	${c1d_core}/DiagnosticAdapter.cpp
	${c1d_core}/FrameMath.cpp
	${c1d_core}/TrustedTimeMap.cpp
	${c1d_core}/WireAdapter.cpp
	${c1d_core}/NativeClosedDiagnosticOwner.cpp
)
target_include_directories(research_c1d_native_core PUBLIC ${c1d_core} "${PX4_SOURCE_DIR}/src/lib")
target_compile_definitions(research_c1d_native_core PRIVATE
	RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 RESEARCH_C1D_CLOSED_OWNER_TARGET=1)
target_compile_features(research_c1d_native_core PUBLIC cxx_std_17)
target_compile_options(research_c1d_native_core PRIVATE -fstack-usage)

px4_add_module(
	MODULE modules__research_native_closed
	MAIN research_native_closed
	EXTERNAL
	SRCS
		research_native_closed.cpp
		../../../bridge/NativeClosedOwnerSourceBridge.cpp
		../../../bridge/NativeClosedJson.cpp
)
target_include_directories(modules__research_native_closed PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../../bridge")
target_link_libraries(modules__research_native_closed PUBLIC research_c1d_native_core)
target_compile_definitions(modules__research_native_closed PRIVATE
	RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 RESEARCH_C1D_CLOSED_OWNER_TARGET=1)
target_compile_features(modules__research_native_closed PUBLIC cxx_std_17)
target_compile_options(modules__research_native_closed PRIVATE -fstack-usage)
