#include "mixer_module.hpp"

#include <cstring>

bool MixingOutput::slaapsInventoryDue() const
{
	const hrt_abstime now = hrt_absolute_time();
	return _slaaps_last_report == 0 || now < _slaaps_last_report || now - _slaaps_last_report >= 100000;
}

void MixingOutput::reportSlaapsInventory(unsigned channel_count, uint32_t hardware_mask, const uint16_t rates[16], bool ready,
					  uint8_t protocol)
{
	static_assert(MAX_ACTUATORS <= 16, "SLAAPS bank message capacity");
	slaaps_output_bank_s report{};
	report.timestamp = hrt_absolute_time();

	if (_slaaps_driver_instance == 0) { _slaaps_driver_instance = report.timestamp; }

	report.driver_instance = _slaaps_driver_instance;
	report.bank = strcmp(_param_prefix, "PWM_MAIN") == 0 ? 0 : strcmp(_param_prefix, "PWM_AUX") == 0 ? 1 : 255;
	report.ready = ready && !_need_function_update && channel_count > 0 && channel_count <= _max_num_outputs;
	report.channel_count = channel_count <= _max_num_outputs ? channel_count : _max_num_outputs;
	report.hardware_mask = hardware_mask;
	report.protocol = protocol;
	report.reversed_mask = _reverse_output_mask;

	for (unsigned i = 0; i < report.channel_count; ++i) {
		report.function[i] = static_cast<uint16_t>(_function_assignment[i]);
		report.rate_hz[i] = rates[i];
		report.minimum_us[i] = _min_value[i];
		report.center_us[i] = _center_value[i];
		report.maximum_us[i] = _max_value[i];
		report.disarmed_us[i] = _disarmed_value[i];
		// Preserve unresolved default sentinel: assuming a safe default here
		// would be incorrect before a function provider has been attached.
		report.failsafe_us[i] = _failsafe_value[i];
	}

	_slaaps_bank_pub.publish(report);
	_slaaps_last_inventory = report;
	initializeSlaapsPreparation();
	_slaaps_last_report = report.timestamp;
}
