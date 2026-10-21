#pragma once

#include "VehicleConfiguration.hpp"

namespace slaaps
{

enum class ControlMode : uint8_t { None, Manual, Acceleration, ServoBench };
enum class EdfUnits : uint8_t { Normalized, Newtons };
enum class EdfFault : uint8_t {
	None, InvalidGrant, InvalidConfiguration, PermissionLost, InvalidTime,
	WrongOwner, InvalidRequest, NotLowThrottle, Expired, Revoked
};
enum class EdfDisposition : uint8_t { Inhibited, Command, RecoveryRequired };

// Supplied by the selected-input/controller adapter; never an actuator topic.
// Tokens identify boot-lifetime owner instances, not MAVLink component IDs.
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
	EdfCommand sample(uint64_t now, const EdfPermission &permission);
	void revoke();
	EdfFault fault() const { return _fault; }
private:
	bool permitted(uint64_t now, const EdfPermission &permission) const;
	EdfFault check(uint64_t now, const EdfPermission &permission);
	EdfFault reject(EdfFault fault);
	VehicleConfiguration _configuration{};
	EdfPermission _grant{};
	EdfRequest _request{};
	uint64_t _last_session{};
	uint64_t _clock{};
	uint64_t _last_sample{};
	uint64_t _lease_us{};
	float _target{};
	float _output{};
	bool _active{};
	bool _received{};
	EdfFault _fault{EdfFault::Revoked};
};

} // namespace slaaps
