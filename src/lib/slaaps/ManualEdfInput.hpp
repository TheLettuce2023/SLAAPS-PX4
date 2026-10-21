#pragma once

#include "EdfOwner.hpp"
#include <lib/research_actuator_command/ManualControlInput.hpp>
#include <lib/mathlib/math/Limits.hpp>
#include <cstdint>

namespace slaaps
{

enum class ManualEdfStatus : uint8_t { Request, Held, Rejected, NotConfigured };

// Selected PX4 throttle to the common EDF owner, without publication or admission.
// Caller must treat Rejected as loss of input and revoke/recover via the supervisor.
// Held never supplies a new request: continue sampling the independently expiring
// owner. Configure once per immutable session; EdfOwner enforces session replay.
class ManualEdfInput
{
public:
	bool configure(const VehicleConfiguration &configuration, const EdfPermission &grant)
	{
		_configured = false;
		_input.reset();

		if (!validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || grant.mode != ControlMode::Manual || grant.session == 0 || grant.source == 0
		    || grant.configuration == 0 || grant.timestamp_us == 0) { return false; }

		research_actuator_command::ManualControlConfiguration mapping{};
		mapping.required_axis_mask = 1U << static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle);
		mapping.source_policy = research_actuator_command::ManualSourcePolicy::MavlinkOnly;
		mapping.maximum_sample_age_us = static_cast<uint64_t>(configuration.manual_age_ms) * 1000;
		mapping.mavlink_instance = configuration.manual_instance;
		mapping.mavlink_system_id = configuration.manual_system;
		mapping.mavlink_component_id = configuration.manual_component;
		// Unity map only reuses the raw-input validator; discard its demand output.
		mapping.demand_map[0][static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)] = 1.F;

		if (_input.configure(mapping) != research_actuator_command::ManualControlStatus::Ok) { return false; }

		_grant = grant;
		_lease_us = math::min(mapping.maximum_sample_age_us, static_cast<uint64_t>(configuration.output_lease_ms) * 1000);
		_sequence = 0;
		_clock = grant.timestamp_us;
		_configured = true;
		return true;
	}

	ManualEdfStatus evaluate(uint64_t now, const research_actuator_command::ManualControlSample &sample,
				 EdfRequest &request)
	{
		if (!_configured) { return ManualEdfStatus::NotConfigured; }

		research_actuator_command::RuntimeInput ignored{};
		research_actuator_command::ManualControlReport report{};

		if (now < _clock || sample.timestamp_sample_us < _grant.timestamp_us
		    || sample.timestamp_sample_us > UINT64_MAX - _lease_us
		    || _input.evaluate(now, sample, ignored, report) != research_actuator_command::ManualControlStatus::Ok
		    || now >= sample.timestamp_sample_us + _lease_us) {
			_configured = false;
			return ManualEdfStatus::Rejected;
		}

		_clock = now;

		if (!report.new_sample) { return ManualEdfStatus::Held; }

		if (_sequence == UINT64_MAX) {
			_configured = false;
			return ManualEdfStatus::Rejected;
		}

		EdfRequest candidate{};
		candidate.session = _grant.session;
		candidate.source = _grant.source;
		candidate.configuration = _grant.configuration;
		candidate.sequence = ++_sequence;
		candidate.timestamp_us = sample.timestamp_sample_us;
		candidate.deadline_us = sample.timestamp_sample_us + _lease_us;
		candidate.mode = ControlMode::Manual;
		candidate.units = EdfUnits::Normalized;
		// PX4 [-1,+1] throttle: centre is 50%, never the low-throttle entry point.
		candidate.value = .5F * (sample.axes[static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)] + 1.F);
		request = candidate;
		return ManualEdfStatus::Request;
	}

private:
	research_actuator_command::ManualControlInput _input{};
	EdfPermission _grant{};
	uint64_t _lease_us{};
	uint64_t _sequence{};
	uint64_t _clock{};
	bool _configured{};
};

} // namespace slaaps
