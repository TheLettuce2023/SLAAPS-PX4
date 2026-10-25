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

		const bool commissioning = grant.mode == ControlMode::EdfBench;

		if (!validate(configuration, commissioning ? ConfigurationUse::EdfCommissioning : ConfigurationUse::StabilizedManual).valid()
		    || (grant.mode != ControlMode::Manual && !commissioning) || grant.session == 0 || grant.source == 0
		    || grant.configuration == 0 || grant.timestamp_us == 0 || grant.session > grant.timestamp_us) { return false; }

		research_actuator_command::ManualControlConfiguration mapping{};
		mapping.required_axis_mask = commissioning ? research_actuator_command::kAllManualAxesMask :
					     1U << static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle);
		mapping.source_policy = configuration.manual_source == static_cast<int32_t>(ManualInputSource::RcOnly) ?
					research_actuator_command::ManualSourcePolicy::RcOnly
					: research_actuator_command::ManualSourcePolicy::MavlinkOnly;
		mapping.maximum_sample_age_us = static_cast<uint64_t>(configuration.manual_age_ms) * 1000;
		mapping.mavlink_instance = configuration.manual_instance;
		mapping.mavlink_system_id = configuration.manual_system;
		mapping.mavlink_component_id = configuration.manual_component;
		// Unity map only reuses the raw-input validator; discard its demand output.
		mapping.demand_map[0][static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)] = 1.F;

		if (commissioning) {
			for (unsigned axis = 0; axis < kAxes; ++axis) { mapping.demand_map[axis][axis] = 1.F; }
		}

		if (_input.configure(mapping) != research_actuator_command::ManualControlStatus::Ok) { return false; }

		_grant = grant;
		_throttle_low = configuration.manual_throttle_low;
		_throttle_high = configuration.manual_throttle_high;
		_maximum = commissioning ? configuration.edf_commission_max : 1.F;
		_entry_max = configuration.edf_entry_max;
		_lease_us = math::min(mapping.maximum_sample_age_us, static_cast<uint64_t>(configuration.output_lease_ms) * 1000);
		_sequence = 0;
		_transfer_us = 0;
		_clock = grant.timestamp_us;
		_configured = true;
		return true;
	}

	// The shared worker supplies its existing EDF sequence after a deliberate
	// Acceleration-to-Manual transition. This does not acquire EDF authority.
	bool configureForTransfer(const VehicleConfiguration &configuration, const EdfPermission &grant,
				  uint64_t transition_us, uint64_t previous_sequence)
	{
		if (!configure(configuration, grant)) { return false; }

		if (grant.mode != ControlMode::Manual || !transition_us || transition_us <= grant.session
		    || grant.transition_us != transition_us || transition_us > grant.timestamp_us
		    || !previous_sequence || previous_sequence == UINT64_MAX) {
			_configured = false;
			return false;
		}

		_sequence = previous_sequence;
		_transfer_us = transition_us;
		return true;
	}

	ManualEdfStatus evaluate(uint64_t now, const research_actuator_command::ManualControlSample &sample,
				 EdfRequest &request)
	{
		if (!_configured) { return ManualEdfStatus::NotConfigured; }

		research_actuator_command::RuntimeInput ignored{};
		research_actuator_command::ManualControlReport report{};

		// Preserve the sample's original clock and expiry. Permission may have
		// been observed after this fresh sample; the deliberate session entry
		// remains the immutable lower bound against pre-entry input reuse.
		if (now < _clock || sample.timestamp_sample_us < _grant.session
		    || (_transfer_us && sample.timestamp_sample_us <= _transfer_us)
		    || sample.timestamp_sample_us > UINT64_MAX - _lease_us
		    || _input.evaluate(now, sample, ignored, report) != research_actuator_command::ManualControlStatus::Ok
		    || now >= sample.timestamp_sample_us + _lease_us) {
			_configured = false;
			return ManualEdfStatus::Rejected;
		}

		_clock = now;
		const float throttle = sample.axes[static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)];

		// Validate the original raw sample before conversion, retaining exact
		// duplicate checks even when the configured throttle range is reversed.
		float value{};

		if (!normalizeManualThrottle(throttle, _throttle_low, _throttle_high, value)) {
			_configured = false;
			return ManualEdfStatus::Rejected;
		}

		if (!report.new_sample) { return ManualEdfStatus::Held; }

		// Check unscaled throttle before applying the commissioning ceiling. A
		// low ceiling cannot turn a high stick into a valid low-throttle entry.
		if (_grant.mode == ControlMode::EdfBench && !_sequence && value > _entry_max) {
			_configured = false;
			return ManualEdfStatus::Rejected;
		}

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
		candidate.mode = _grant.mode;
		candidate.units = EdfUnits::Normalized;
		candidate.value = _grant.mode == ControlMode::EdfBench && _sequence == 1 ? 0.F : value * _maximum;
		request = candidate;
		return ManualEdfStatus::Request;
	}

private:
	research_actuator_command::ManualControlInput _input{};
	EdfPermission _grant{};
	uint64_t _lease_us{};
	uint64_t _sequence{};
	uint64_t _transfer_us{};
	uint64_t _clock{};
	float _throttle_low{}, _throttle_high{};
	float _maximum{1.F}, _entry_max{};
	bool _configured{};
};

} // namespace slaaps
