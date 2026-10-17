set(c1e_core "${PX4_SOURCE_DIR}/src/lib/research_acceleration")
set(c1e_parser "${PX4_SOURCE_DIR}/Tools/research_sitl/diagnostic_wire/native_parser")
if(NOT C1E_GENERATED_MAVLINK_DIR OR NOT EXISTS "${C1E_GENERATED_MAVLINK_DIR}/diagnostic/mavlink.h")
	message(FATAL_ERROR "C1E native target requires pinned generated diagnostic MAVLink headers")
endif()

add_library(research_c1e_native_core STATIC
	${c1e_core}/RequestGuard.cpp
	${c1e_core}/DiagnosticGuard.cpp
	${c1e_core}/DiagnosticAdapter.cpp
	${c1e_core}/FrameMath.cpp
	${c1e_core}/TrustedTimeMap.cpp
	${c1e_core}/WireAdapter.cpp
	${c1e_core}/NativeIngressDiagnosticOwner.cpp
)
target_include_directories(research_c1e_native_core PUBLIC ${c1e_core} "${PX4_SOURCE_DIR}/src/lib")
target_compile_definitions(research_c1e_native_core PRIVATE
	RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 RESEARCH_C1E_NEGATIVE_OWNER_TARGET=1)
target_compile_features(research_c1e_native_core PUBLIC cxx_std_17)
target_compile_options(research_c1e_native_core PRIVATE -fstack-usage)

add_library(research_c1e_native_parser STATIC ${c1e_parser}/DiagnosticSignedMavlink.cpp)
target_include_directories(research_c1e_native_parser PUBLIC ${c1e_parser} "${C1E_GENERATED_MAVLINK_DIR}")
target_compile_definitions(research_c1e_native_parser PRIVATE
	RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 RESEARCH_C1E_NEGATIVE_OWNER_TARGET=1)
target_compile_features(research_c1e_native_parser PUBLIC cxx_std_17)
target_compile_options(research_c1e_native_parser PRIVATE -fstack-usage)

px4_add_module(
	MODULE modules__research_native_ingress
	MAIN research_native_ingress
	EXTERNAL
	SRCS
		research_native_ingress.cpp
		../../../bridge/NativeIngressSourceBridge.cpp
		../../../bridge/NativeIngressWorker.cpp
		../../../bridge/NativeIngressJson.cpp
		../../../../c1d_closed/bridge/NativeClosedJson.cpp
)
target_include_directories(modules__research_native_ingress PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/../../../bridge"
	"${CMAKE_CURRENT_LIST_DIR}/../../../../c1d_closed/bridge")
target_link_libraries(modules__research_native_ingress PUBLIC research_c1e_native_core research_c1e_native_parser)
target_compile_definitions(modules__research_native_ingress PRIVATE
	RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 RESEARCH_C1E_NEGATIVE_OWNER_TARGET=1)
target_compile_features(modules__research_native_ingress PUBLIC cxx_std_17)
target_compile_options(modules__research_native_ingress PRIVATE -fstack-usage)
