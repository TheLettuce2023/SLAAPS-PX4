#pragma once

#include "EdfOwner.hpp"

namespace slaaps
{

// Supplied by the supervisor after routing admission, never inferred from an ack.
struct ManualOutputBinding {
	uint64_t routing{};
	uint64_t driver[2] {}; // MAIN, AUX lifetime tokens.
	uint8_t banks{};
};

// Driver handled the complete bank portion of one combined command. This is not
// proof of electrical application. Transport must bind the actual publisher.
struct ManualOutputAck {
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	uint64_t routing{};
	uint64_t driver{};
	uint64_t sequence{};
	uint64_t command_timestamp_us{};
	uint64_t accepted_us{};
	uint8_t bank{}; // 0 MAIN, 1 AUX.
};

// Bank entered its initial command-wait state. This is neither an actuator
// handling acknowledgment nor flight permission. Transport binds its publisher.
struct ManualActivationAck {
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	uint64_t routing{};
	uint64_t driver{};
	uint64_t activated_us{};
	uint64_t first_command_deadline_us{};
	unsigned command_revision{};
	unsigned acknowledgment_revision{};
	uint8_t bank{};
};

inline bool sameManualActivation(const ManualActivationAck &a, const ManualActivationAck &b)
{
	return a.session == b.session && a.source == b.source && a.configuration == b.configuration
	       && a.routing == b.routing && a.driver == b.driver && a.activated_us == b.activated_us
	       && a.first_command_deadline_us == b.first_command_deadline_us && a.command_revision == b.command_revision
	       && a.acknowledgment_revision == b.acknowledgment_revision && a.bank == b.bank;
}

struct ManualWorkerOutput {
	uint64_t timestamp_us{};
	uint64_t deadline_us{};
	uint64_t sequence{}; // Combined control cycles; EDF has its own raw-input sequence.
	research_stabilized_manual::CanardVector canards{};
	EdfCommand edf{};
	ManualOutputBinding binding{};
	ControlMode mode{ControlMode::Manual};
};

inline bool validManualOutputBinding(const ManualOutputBinding &binding)
{
	if (!binding.routing || !binding.banks || (binding.banks & ~3U)) { return false; }

	for (unsigned bank = 0; bank < 2; ++bank) {
		if (static_cast<bool>(binding.banks & (1U << bank)) != (binding.driver[bank] != 0)) { return false; }
	}

	return true;
}
inline bool sameManualOutputBinding(const ManualOutputBinding &a, const ManualOutputBinding &b)
{
	return a.routing == b.routing && a.banks == b.banks && a.driver[0] == b.driver[0] && a.driver[1] == b.driver[1];
}

} // namespace slaaps
