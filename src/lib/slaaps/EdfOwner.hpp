#pragma once

#include "VehicleConfiguration.hpp"

namespace slaaps
{

enum class ControlMode : uint8_t { None, Manual, Acceleration, ServoBench, EdfBench = 4 };
enum class EdfUnits : uint8_t { Normalized, Newtons };
enum class EdfFault : uint8_t {
	None, InvalidGrant, InvalidConfiguration, PermissionLost, InvalidTime,
	WrongOwner, InvalidRequest, NotLowThrottle, Expired, Revoked
};
enum class EdfDisposition : uint8_t { Inhibited, Command, RecoveryRequired };

// Supplied by the selected-input/controller adapter; never an actuator topic.
// Session is the immutable monotonic HRT entry timestamp shared with permission;
// source identifies the boot-lifetime owner, not a MAVLink component ID.
struct EdfRequest {
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	uint64_t sequence{};
	uint64_t timestamp_us{};
	uint64_t deadline_us{};
	ControlMode mode{ControlMode::None};
	EdfUnits units{EdfUnits::Normalized};
	float value{};
};

// A fresh snapshot from the supervisor, independently checked at every sample.
// flight_admitted requires a validated transition/loss policy and vehicle data;
// neither a parameter value nor successful configuration validation grants it.
// Session is minted at deliberate preparation/entry and bounds request sample
// times. It is not the current arming time or renewable permission publication
// time; a valid post-entry sample may precede timestamp_us.
struct EdfPermission {
	uint64_t timestamp_us{};
	uint64_t deadline_us{};
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	ControlMode mode{ControlMode::None};
	bool armed{};
	bool flight_admitted{};
	bool healthy{};
	bool measured_condition_valid{}; // EDF curve voltage/condition, not nominal voltage
	bool kill{};
	bool lockdown{};
	bool termination{};
	bool prearmed{}; // Actual actuator_armed prearm state; never propulsion permission.
	bool commissioning_admitted{}; // Explicit ground commissioning, never flight admission.
	uint64_t transition_us{}; // Zero on begin; exact retained supervisor transfer marker afterward.
};

struct EdfCommand {
	EdfDisposition disposition{EdfDisposition::Inhibited};
	float normalized{};
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	uint64_t sequence{};
	uint64_t request_timestamp_us{};
	uint64_t deadline_us{}; // Never renewed by polling or holding the same request.
};

// One serialized owner for both flight modes. This class does not publish,
// choose an airborne fallback, or replace independent driver-side expiry.
// Preserve it across worker replacement so old session tokens cannot re-enter.
class EdfOwner
{
public:
	EdfOwner() = default;
	EdfOwner(const EdfOwner &) = delete;
	EdfOwner &operator=(const EdfOwner &) = delete;
	EdfFault begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission);
	EdfFault accept(uint64_t now, const EdfPermission &permission, const EdfRequest &request);
	// Serialized supervisor transfer within one flight output session. Both
	// grants keep source/configuration/session; only Manual <-> Acceleration
	// changes. A fresh post-transition request keeps increasing EDF sequence
	// and cannot outlive the outgoing request or either permission. No fallback
	// target is synthesized. The returned current sample is not a driver ACK.
	EdfFault transfer(uint64_t now, uint64_t transition_us, const EdfPermission &previous,
			  const EdfPermission &next, const EdfRequest &request, EdfCommand &current);
	EdfCommand sample(uint64_t now, const EdfPermission &permission);
	void revoke();
	EdfFault fault() const { return _fault; }
private:
	bool permitted(uint64_t now, const EdfPermission &permission) const;
	bool permitted(uint64_t now, const EdfPermission &permission, const EdfPermission &grant) const;
	EdfFault check(uint64_t now, const EdfPermission &permission);
	EdfFault reject(EdfFault fault);
	VehicleConfiguration _configuration{};
	EdfPermission _grant{};
	EdfRequest _request{};
	uint64_t _last_session{};
	uint64_t _last_transition_us{}; // Consumed transitions survive revoke/begin.
	uint64_t _clock{};
	uint64_t _last_sample{};
	uint64_t _started_us{};
	uint64_t _lease_us{};
	float _target{};
	float _output{};
	bool _active{};
	bool _received{};
	EdfFault _fault{EdfFault::Revoked};
};

} // namespace slaaps
