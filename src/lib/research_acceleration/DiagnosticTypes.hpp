#pragma once

#include "DiagnosticBuildProfile.hpp"
#if !RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
#error "Diagnostic types require the isolated offline test target or an explicit native profile"
#endif

#include <cstdint>

namespace research_acceleration
{

enum class DiagnosticExecutionOrigin : uint8_t { SyntheticOffline, SyntheticNativeContract, NativePosixSitl };

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
struct DiagnosticOfflineTag {};
inline constexpr DiagnosticExecutionOrigin kDiagnosticExecutionOrigin = DiagnosticExecutionOrigin::SyntheticOffline;
#else
struct DiagnosticNativeSitlTag {};
#if defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST)
inline constexpr DiagnosticExecutionOrigin kDiagnosticExecutionOrigin = DiagnosticExecutionOrigin::SyntheticNativeContract;
#else
inline constexpr DiagnosticExecutionOrigin kDiagnosticExecutionOrigin = DiagnosticExecutionOrigin::NativePosixSitl;
#endif
#endif

enum class DiagnosticSelectionState : uint8_t {
	Disabled = 0, SelectedForValidation = 1, Faulted = 2
};

// Local diagnostic values only. They have no frozen-v1 wire representation.
enum class DiagnosticCode : uint8_t {
	Observed = 0, NoEligibleRequest = 1, SelectionEnabled = 2, SelectionDisabled = 3,
	AlreadyDisabled = 4, RequestValidatedDisarmed = 5, Rejected = 6, Terminal = 7,
	NotConfigured = 8, WrongDomain = 9, SessionEstablished = 10, SessionRevoked = 11,
	LatchCleared = 12, Acknowledged = 13
};

enum class DiagnosticLocalReason : uint8_t {
	None, NotDisarmed, FlightModeConflict, SelectorInvalid, SelectorFaulted, SelectionRequired,
	BindingMismatch, ContextChanged, GrantInvalid, GrantReplay, CounterExhausted, TrustedStateFault
};

enum class DiagnosticGuardOperation : uint8_t { Request, Poll, Establish, Clear };

} // namespace research_acceleration
