#pragma once

#include "ReportedRouting.hpp"
#include "ManualOutput.hpp"

namespace slaaps
{

struct ReportedRoutingPolicy {
	uint64_t maximum_capture_age_us{};
	uint64_t maximum_report_age_us{};
};

// Bind trusted capture results to a pending/admitted routing identity. No
// permission is granted. Failure latches until explicit begin; a fresh report
// from a replacement driver cannot silently repair an existing transaction.
class ObservedRoutingBinding
{
public:
	bool begin(uint64_t now, const VehicleConfiguration &configuration, const ReportedRoutingSnapshot &snapshot,
		   InventoryFault capture_result, const ManualOutputBinding &binding, const ReportedRoutingPolicy &policy)
	{
		_valid = false;
		_policy = policy;

		if (!observationValid(now, snapshot, capture_result) || !validManualOutputBinding(binding)
		    || !validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || !makeRoutingPlan(configuration, snapshot.inventory, true, _routing).valid()
		    || _routing.bank_mask != binding.banks) { return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if ((binding.banks & (1U << bank)) && binding.driver[bank] != snapshot.driver[bank]) { return false; }

			_driver[bank] = snapshot.driver[bank];
			_revision[bank] = snapshot.publisher_revision[bank];
			_instance[bank] = snapshot.topic_instance[bank];
			_reported[bank] = snapshot.reported_us[bank];
		}

		_configuration = fingerprint(configuration);
		_captured = snapshot.captured_us;
		_clock = now;
		_valid = true;
		return true;
	}

	bool check(uint64_t now, const VehicleConfiguration &configuration, const ReportedRoutingSnapshot &snapshot,
		   InventoryFault capture_result)
	{
		RoutingPlan routing{};

		if (!_valid || now < _clock || !observationValid(now, snapshot, capture_result)
		    || snapshot.captured_us < _captured || fingerprint(configuration) != _configuration
		    || !makeRoutingPlan(configuration, snapshot.inventory, true, routing).valid() || !sameRoutingPlan(routing, _routing)) {
			_valid = false;
			return false;
		}

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (snapshot.driver[bank] != _driver[bank] || snapshot.publisher_revision[bank] != _revision[bank]
			    || snapshot.topic_instance[bank] != _instance[bank] || snapshot.reported_us[bank] < _reported[bank]) {
				_valid = false;
				return false;
			}
		}

		for (unsigned bank = 0; bank < 2; ++bank) { _reported[bank] = snapshot.reported_us[bank]; }

		_clock = now;
		_captured = snapshot.captured_us;
		return true;
	}
	void cancel() { _valid = false; }
private:
	bool observationValid(uint64_t now, const ReportedRoutingSnapshot &snapshot, InventoryFault capture_result) const
	{
		if (capture_result != InventoryFault::None || !now || !_policy.maximum_capture_age_us || !_policy.maximum_report_age_us
		    || _policy.maximum_report_age_us > kMaximumOutputReportAgeUs || !snapshot.captured_us || snapshot.captured_us > now
		    || now - snapshot.captured_us >= _policy.maximum_capture_age_us || snapshot.topic_instance[0] == snapshot.topic_instance[1]) { return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!snapshot.driver[bank] || !snapshot.publisher_revision[bank] || !snapshot.reported_us[bank]
			    || snapshot.reported_us[bank] > snapshot.captured_us
			    || now - snapshot.reported_us[bank] >= _policy.maximum_report_age_us) { return false; }
		}

		return true;
	}
	RoutingPlan _routing{};
	ReportedRoutingPolicy _policy{};
	uint64_t _configuration{}, _clock{}, _captured{}, _driver[2] {}, _reported[2] {};
	unsigned _revision[2] {};
	uint8_t _instance[2] {};
	bool _valid{};
};

} // namespace slaaps
