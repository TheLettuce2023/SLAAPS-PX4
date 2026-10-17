#pragma once

// This selector is derived here, never a build-system bypass.
#if defined(RESEARCH_SITL_DIAGNOSTIC_CAPABILITY)
#error "Diagnostic capability must be derived from a validated profile"
#endif

#if (defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE) + defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST) \
     + defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)) > 1
#error "Diagnostic build profiles are mutually exclusive"
#endif

#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) && defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
#error "C1E native and synthetic target markers are mutually exclusive"
#endif

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
#if RESEARCH_SITL_DIAGNOSTIC_OFFLINE != 1
#error "Diagnostic types require the isolated offline test target"
#endif
#if !defined(RESEARCH_SITL_DIAGNOSTIC_HOST_TEST) || RESEARCH_SITL_DIAGNOSTIC_HOST_TEST != 1
#error "Diagnostic types require the standalone host-test build"
#endif
#if !defined(__linux__) || defined(__PX4_NUTTX) || defined(__PX4_POSIX) || defined(__PX4_LINUX) \
	|| defined(__PX4_ROS2) || defined(__NuttX__) || defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP) || defined(RESEARCH_C1D_CLOSED_OWNER_TARGET) \
	|| defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
#error "Diagnostic validation is not released for PX4, hardware, lockstep or timestamp proxies"
#endif
#define RESEARCH_SITL_DIAGNOSTIC_CAPABILITY 1

#elif defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST)
#if RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST != 1 || !defined(__linux__) \
	|| defined(RESEARCH_SITL_DIAGNOSTIC_HOST_TEST) || defined(RESEARCH_C1D_CLOSED_OWNER_TARGET) \
	|| defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) \
	|| defined(__PX4_NUTTX) || defined(__PX4_POSIX) || defined(__PX4_LINUX) || defined(__PX4_ROS2) \
	|| defined(__NuttX__) || defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Native diagnostic contract tests require isolated Linux host provenance"
#endif
#if defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST) && RESEARCH_C1E_NEGATIVE_CONTRACT_TEST != 1
#error "C1E synthetic target marker must equal one"
#endif
#define RESEARCH_SITL_DIAGNOSTIC_CAPABILITY 1

#elif defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
#if RESEARCH_SITL_DIAGNOSTIC_NATIVE != 1 \
	|| (defined(RESEARCH_C1D_CLOSED_OWNER_TARGET) + defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET)) != 1 \
	|| (defined(RESEARCH_C1D_CLOSED_OWNER_TARGET) && RESEARCH_C1D_CLOSED_OWNER_TARGET != 1) \
	|| (defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) && RESEARCH_C1E_NEGATIVE_OWNER_TARGET != 1) \
	|| defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST) || defined(RESEARCH_SITL_DIAGNOSTIC_HOST_TEST) \
	|| !defined(__linux__) \
	|| defined(__PX4_NUTTX) || defined(__PX4_ROS2) || defined(__NuttX__) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Native diagnostic profile requires dedicated target and forbids host/hardware/lockstep/proxy markers"
#else
#include <px4_platform_common/px4_config.h>
#if !defined(__PX4_POSIX) || !defined(CONFIG_ARCH_BOARD_PX4_SITL) \
	|| defined(__PX4_NUTTX) || defined(__PX4_ROS2) || defined(__NuttX__) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Native diagnostic validation requires dedicated native Linux POSIX PX4 SITL"
#endif
#endif
#define RESEARCH_SITL_DIAGNOSTIC_CAPABILITY 1

#else
#if defined(RESEARCH_SITL_DIAGNOSTIC_HOST_TEST) || defined(RESEARCH_C1D_CLOSED_OWNER_TARGET) \
	|| defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
#error "Diagnostic target marker requires its matching profile"
#endif
#define RESEARCH_SITL_DIAGNOSTIC_CAPABILITY 0
#endif
