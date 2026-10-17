#pragma once

#include "BenchControlOwner.hpp"
#include <cstdio>

namespace research_actuator_command
{

struct BenchOutputProfile {
	BenchConfiguration control{};
	float deadzone{};
	RuntimeTimeUs lease_us{};
};

// Versioned fixed-token text profile. Failure preserves the caller's profile.
bool readBenchOutputProfile(FILE *stream, BenchOutputProfile &profile);
float benchDeadzone(float value, float deadzone);
bool benchPwmToControl(const BenchVector &pwm, const BenchConfiguration &configuration, BenchVector &control);

struct BenchOutputFrame {
	RuntimeTimeUs timestamp{};
	RuntimeTimeUs session{};
	RuntimeTimeUs lease_us{};
	BenchVector control{};
	bool enabled{};
};

// Consumer-side expiry latch, independent of the command owner's scheduling.
// No frame at boot => NaN/disarmed. After a valid enable, expiry/revoke => zero
// (driver-calibrated neutral) until a strictly newer explicit session arrives.
class BenchOutputLease
{
public:
	void update(RuntimeTimeUs now, const BenchOutputFrame &frame);
	float value(RuntimeTimeUs now, std::size_t channel);
	void revoke() { _fault = true; }
	bool faulted() const { return _fault; }
private:
	BenchOutputFrame _frame{};
	RuntimeTimeUs _session{};
	RuntimeTimeUs _timestamp{};
	bool _ever_enabled{};
	bool _fault{};
};

} // namespace research_actuator_command
