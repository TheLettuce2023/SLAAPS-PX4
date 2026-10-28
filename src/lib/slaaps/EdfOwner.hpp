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
// Recovery is only produced by EdfOwner::recover(); wire and worker paths accept Command alone.
enum class EdfDisposition : uint8_t { Inhibited, Command, RecoveryRequired, Recovery };

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
	// D3/D4 airborne recovery. After a flight grant is lost to PermissionLost or Expired the owner
	// ramps EDF to off at SLA_EDF_FALL from its last sampled output, and forces off no later than
	// kRecoveryCeilingUs after detection. It needs only an armed, non-kill, non-lockdown, non-termination
	// permission of the same session; it never revives the grant, accepts requests or extends a lease.
	// Each sample's deadline is one output lease from now, capped by the ceiling. The caller must still
	// hold canards at their last handled position (D3); that bank action is not part of this class.
	EdfCommand recover(uint64_t now, const EdfPermission &permission);
	bool recovering() const { return _recovery_active; }
	// Caller-detected loss (estimator, bank or control fault) of an actively granted flight session.
	// Starts the same recovery as an owner-detected loss. The worker alone decides when to call it.
	// A D3 escalation during a D1 hold passes the hold's start, so D4 still counts from the first loss.
	void abandon(uint64_t now, uint64_t started_us = 0);
	static constexpr uint64_t kRecoveryCeilingUs = kRecoveryLimitUs;
	// D1 owned sequence target: the held command for SLA_REC_HOLD, then a SLA_REC_RAMP slew to
	// SLA_REC_THR, never above the line that reaches zero at the D4 limit. The worker submits it as an
	// ordinary new request; it is a decided response, never reuse of the lost demand.
	static float holdTarget(const RecoveryConfiguration &recovery, float held, uint64_t elapsed_us);
	// D1 entry. The lost input's last request may already have expired: its lease is the same bound that detects
	// the loss. Install the first owned hold request at the current slewed output instead. The flight grant must
	// still be fully permitted (a lost permission starts D3 as before); only that last request's expiry is passed
	// over, once, while the owner is active. held receives the command the hold keeps.
	EdfFault acceptHold(uint64_t now, const EdfPermission &permission, uint64_t deadline_us, float &held);
	void revoke();
	EdfFault fault() const { return _fault; }
private:
	bool permitted(uint64_t now, const EdfPermission &permission) const;
	bool permitted(uint64_t now, const EdfPermission &permission, const EdfPermission &grant) const;
	EdfFault check(uint64_t now, const EdfPermission &permission);
	EdfFault reject(EdfFault fault);
	EdfFault lose(uint64_t now, EdfFault fault);
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
	bool _recovery_active{};
	uint64_t _recovery_start_us{};
	float _recovery_output{};
	uint64_t _recovery_last_us{};
	EdfFault _fault{EdfFault::Revoked};
};

} // namespace slaaps
