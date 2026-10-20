#pragma once

#include "StabilizedManualControl.hpp"

namespace research_stabilized_manual
{

// Explicit calibration of allocator -1/0/+1 to pulse width. Reversal belongs
// here exactly once. Driver endpoints describe the configured downstream mixer.
struct OutputServoCalibration {
	float negative_pwm_us{};
	float zero_pwm_us{};
	float positive_pwm_us{};
	uint16_t driver_minimum_us{};
	uint16_t driver_center_us{};
	uint16_t driver_maximum_us{};
};

enum class OutputFallback : uint8_t { Unspecified, Disarmed, Calibrated };
struct OutputConfiguration {
	research_actuator_command::FixedArray<OutputServoCalibration, 4> servos{};
	CanardVector minimum{};
	CanardVector maximum{};
	CanardVector fallback{};
	TimeUs lease_us{};
	TimeUs acknowledgement_us{};
	OutputFallback on_fault{OutputFallback::Unspecified};
};

enum class OutputState : uint8_t { Disabled, Awaiting, Active, Fault };
enum class OutputStatus : uint8_t {
	Ok, NoCommand, Waiting, InvalidConfiguration, InvalidState, InvalidTime,
	PermissionLost, InvalidCommand, InvalidAcknowledgement, Expired
};
enum class OutputDisposition : uint8_t { Disarmed, Command, Fallback };

struct OutputFrame {
	TimeUs session{};
	TimeUs timestamp_us{}; // Successful control calculation time, never republished as fresh.
	uint64_t sequence{};
	CanardVector command{};
};

// Confirms consumer validation and candidate mapping, NOT electrical application.
struct OutputAcknowledgement {
	TimeUs session{};
	TimeUs command_timestamp_us{};
	TimeUs accepted_us{};
	uint64_t sequence{};
};

struct OutputCandidate {
	OutputDisposition disposition{OutputDisposition::Disarmed};
	CanardVector pwm_us{};
	CanardVector driver_control{};
};

bool validOutputConfiguration(const OutputConfiguration &configuration);
// Atomic four-channel conversion. Failure leaves the caller candidate unchanged.
bool mapStabilizedOutput(const OutputConfiguration &configuration, const CanardVector &command,
			 OutputCandidate &candidate);

// Portable protocol only: no uORB, driver, permission inference or auto-entry.
// Both endpoints are serialized by their own caller. begin() is a deliberate,
// out-of-band grant with the SAME immutable configuration/session on both ends.
// A session is a strictly increasing boot-clock token, preserved across endpoint
// object replacement by the future supervisor. Messages can never grant a session.
class StabilizedOutputProducer
{
public:
	StabilizedOutputProducer() = default;
	StabilizedOutputProducer(const StabilizedOutputProducer &) = delete;
	StabilizedOutputProducer &operator=(const StabilizedOutputProducer &) = delete;
	// Poll before advancing the controller: Waiting means do not integrate again.
	OutputStatus begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration, bool permitted);
	OutputStatus poll(TimeUs now, bool permitted);
	OutputStatus submit(TimeUs now, bool permitted, Status calculation_status, const Report &report, OutputFrame &frame);
	OutputStatus acknowledge(TimeUs now, bool permitted, const OutputAcknowledgement &acknowledgement);
	void disable();
	OutputState state() const { return _state; }
	OutputStatus fault() const { return _fault; }

private:
	OutputStatus reject(OutputStatus status);
	OutputConfiguration _configuration{};
	OutputFrame _pending{};
	TimeUs _session{};
	TimeUs _clock{};
	TimeUs _last_command{};
	uint64_t _sequence{};
	bool _waiting{};
	OutputState _state{OutputState::Disabled};
	OutputStatus _fault{OutputStatus::Ok};
};

// Runs on the output consumer's clock, independently of the controller. Call
// sample() on EVERY output dispatch, even when no new frame arrives. The returned
// disposition must be honored; zero-filled Disarmed data is not a zero-us command.
// Expiry and permission loss latch; delayed traffic cannot restore authority.
class StabilizedOutputConsumer
{
public:
	StabilizedOutputConsumer() = default;
	StabilizedOutputConsumer(const StabilizedOutputConsumer &) = delete;
	StabilizedOutputConsumer &operator=(const StabilizedOutputConsumer &) = delete;
	OutputStatus begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration, bool permitted);
	OutputStatus accept(TimeUs now, bool permitted, const OutputFrame &frame, OutputAcknowledgement &acknowledgement);
	OutputCandidate sample(TimeUs now, bool permitted);
	void disable();
	OutputState state() const { return _state; }
	OutputStatus fault() const { return _fault; }

private:
	OutputStatus poll(TimeUs now, bool permitted);
	OutputStatus reject(OutputStatus status);
	OutputConfiguration _configuration{};
	OutputCandidate _candidate{};
	TimeUs _session{};
	TimeUs _clock{};
	TimeUs _last_command{};
	uint64_t _sequence{};
	bool _inhibited{true}; // Permission loss suppresses even calibrated fallback until a new grant.
	OutputState _state{OutputState::Disabled};
	OutputStatus _fault{OutputStatus::Ok};
};

} // namespace research_stabilized_manual
