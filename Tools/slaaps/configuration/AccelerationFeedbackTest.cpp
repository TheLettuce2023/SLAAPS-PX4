#include <lib/slaaps/AccelerationFeedback.hpp>

#include <cstdio>
#include <cmath>
#include <cstring>

namespace
{
using slaaps::AccelerationFeedback;
using slaaps::AccelerationFeedbackInput;
using slaaps::AccelerationFeedbackResult;
using slaaps::AccelerationFeedbackSample;
using research_stabilized_manual::Vector3f;

slaaps::VehicleConfiguration configuration()
{
	slaaps::VehicleConfiguration c{};
	c.feedback.enabled = 1;
	c.feedback.maximum_age_us = 100000;
	c.feedback.maximum_bias_age_us = 100000;
	c.feedback.latency_bound_us = 5000;
	c.feedback.settling_us = 10000;
	c.feedback.maximum_specific_force_m_s2 = 100.F;
	c.feedback.maximum_acceleration_m_s2 = 100.F;
	c.feedback.gravity_m_s2 = 10.F; // Explicit synthetic gravity, not deployment data.
	c.feedback.cutoff_hz = 10.F;
	c.state_age_ms = 200;
	c.state_skew_ms = 20;
	c.selected_status_age_us = 1000000;
	c.quaternion_tolerance = 0.01F;

	for (auto &axis : c.axis) {
		axis.measured_rate_limit = 10.F;
		axis.measured_accel_limit = 100.F;
	}

	return c;
}

AccelerationFeedbackInput input(uint64_t now)
{
	AccelerationFeedbackInput value{};
	value.observed_us = now;
	value.timestamp_us = now - 100;
	value.timestamp_sample_us = now - 200;
	value.source_generation = 3;
	value.publisher_revision = 5;
	value.device_id = value.selected_accel_device_id = 456;
	value.selected_gyro_device_id = 123;
	value.raw_publisher_revision = 6;
	value.raw_publisher_current = true;
	value.filter_cutoff_hz = 10.F;
	value.filter_sample_rate_hz = 1000.F;
	value.calibrated = true;
	value.bias_timestamp = now - 1000;
	value.bias_timestamp_sample = now - 1200;
	value.bias_publisher_revision = 7;
	value.bias_valid = value.bias_publisher_current = true;
	value.specific_force_frd_m_s2 = Vector3f(1.F, 2.F, -7.F);
	value.state.valid = true;
	value.state.source_epoch = 1;
	value.state.attitude = research_stabilized_manual::Quaternion(1.F, 0.F, 0.F, 0.F);
	value.state.attitude_timestamp_us = value.state.rate_timestamp_us = now - 100;
	value.state.attitude_sample_us = value.state.rate_sample_us = now - 200;
	return value;
}

bool same(const AccelerationFeedbackSample &left, const AccelerationFeedbackSample &right)
{
	if (left.observed_us != right.observed_us || left.timestamp_sample_us != right.timestamp_sample_us
	    || left.deadline_us != right.deadline_us || left.source_generation != right.source_generation
	    || left.publisher_revision != right.publisher_revision || left.device_id != right.device_id) { return false; }

	for (unsigned axis = 0; axis < 3; ++axis) {
		const float a = left.acceleration_frd_m_s2(axis), b = right.acceleration_frd_m_s2(axis);

		if (std::memcmp(&a, &b, sizeof(float)) != 0) { return false; }
	}

	return true;
}

bool near(const Vector3f &value, float x, float y, float z)
{
	return std::fabs(value(0) - x) < 1e-4F && std::fabs(value(1) - y) < 1e-4F && std::fabs(value(2) - z) < 1e-4F;
}
}

// Called by the existing portable/native configuration fixture. No new runner.
bool slaapsAccelerationFeedbackTests(unsigned &checks)
{
	bool passed = true;
	auto expect = [&](bool condition, const char *description) {
		++checks;

		if (!condition) { std::fprintf(stderr, "FAIL acceleration feedback: %s\n", description); passed = false; }
	};
	constexpr uint64_t start = 1000000;
	constexpr uint64_t ready_time = start + 12000;
	const auto c = configuration();
	const auto first = input(start);
	const auto ready = input(ready_time);
	AccelerationFeedbackSample output{};
	output.observed_us = 73;
	output.acceleration_frd_m_s2 = Vector3f(9.F, 8.F, 7.F);
	const auto sentinel = output;
	AccelerationFeedback feedback;
	expect(feedback.configure(c), "explicit synthetic feedback policy configures");
	expect(feedback.evaluate(start, first, output) == AccelerationFeedbackResult::Settling && same(output, sentinel),
	       "initial observation conservatively starts settling without changing destination");
	expect(feedback.evaluate(ready_time, first, output) == AccelerationFeedbackResult::Settling && same(output, sentinel),
	       "wall clock cannot promote retained pre-settling measurement");
	expect(feedback.evaluate(ready_time, ready, output) == AccelerationFeedbackResult::Ready
	       && near(output.acceleration_frd_m_s2, 1.F, 2.F, 3.F), "body specific force plus gravity yields inertial acceleration");
	const uint64_t original_deadline = ready.timestamp_sample_us + 95001;
	expect(output.deadline_us == original_deadline && output.timestamp_sample_us == ready.timestamp_sample_us,
	       "original raw expiry is reduced by declared latency");
	expect(feedback.evaluate(ready_time + 20000, ready, output) == AccelerationFeedbackResult::Ready
	       && output.deadline_us == original_deadline, "held observations never renew original expiry");
	const auto held = output;
	expect(feedback.evaluate(original_deadline, ready, output) == AccelerationFeedbackResult::Stale && same(output, held),
	       "original exclusive expiry rejects and preserves destination");
	auto resumed = input(original_deadline + 1000);
	expect(feedback.evaluate(resumed.observed_us, resumed, output) == AccelerationFeedbackResult::Settling,
	       "fresh data after expiry must settle again");
	resumed = input(original_deadline + 13000);
	expect(feedback.evaluate(resumed.observed_us, resumed, output) == AccelerationFeedbackResult::Ready,
	       "fresh post-settling source can restore observation without restoring any protocol session");

	auto lever_configuration = c;
	lever_configuration.feedback.imu_position[0] = 1.F;
	lever_configuration.cg[0] = 2.F;
	lever_configuration.cg[1] = 3.F;
	lever_configuration.cg[2] = 4.F;
	auto lever_first = first, lever_ready = ready;

	AccelerationFeedbackInput *lever_samples[] {&lever_first, &lever_ready};

	for (auto *sample : lever_samples) {
		sample->state.rate_rad_s = Vector3f(0.F, 0.F, 2.F);
		sample->state.angular_accel_rad_s2 = Vector3f(0.F, 0.F, 3.F);
		sample->specific_force_frd_m_s2 = Vector3f(-3.F, 5.F, -7.F);
	}

	expect(feedback.configure(lever_configuration), "nonzero CG-to-IMU offset configures without applying cg twice");
	feedback.evaluate(start, lever_first, output);
	expect(feedback.evaluate(ready_time, lever_ready, output) == AccelerationFeedbackResult::Ready
	       && near(output.acceleration_frd_m_s2, 1.F, 2.F, 3.F), "tangential and centripetal lever terms have correct sign");
	auto nose_first = first, nose_ready = ready;

	AccelerationFeedbackInput *nose_samples[] {&nose_first, &nose_ready};

	for (auto *sample : nose_samples) {
		sample->state.attitude = research_stabilized_manual::Quaternion(0.70710677F, 0.F, 0.70710677F, 0.F);
		sample->specific_force_frd_m_s2 = Vector3f(10.F, 0.F, 0.F);
	}

	feedback.configure(c);
	feedback.evaluate(start, nose_first, output);
	expect(feedback.evaluate(ready_time, nose_ready, output) == AccelerationFeedbackResult::Ready
	       && near(output.acceleration_frd_m_s2, 0.F, 0.F, 0.F), "gravity rotates into current body frame for nose-up orientation");

	auto rejects = [&](const AccelerationFeedbackInput & sample, AccelerationFeedbackResult expected, const char *description) {
		AccelerationFeedback probe;
		const bool configured = probe.configure(c);
		output = sentinel;
		const auto result = probe.evaluate(sample.observed_us, sample, output);
		expect(configured && result == expected && same(output, sentinel), description);
	};
	auto bad = first;
	bad.raw_publisher_current = false;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "independent live raw loss rejects retained acceleration metadata");
	bad = first; bad.bias_publisher_current = false;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "independent live bias loss rejects historical bias_valid");
	bad = first; bad.calibrated = false;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "native calibration must be present");
	bad = first; ++bad.device_id;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "actual accelerometer must match selected device");
	bad = first; bad.source_generation = 0;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "exhausted source generation cannot be accepted");
	bad = first; bad.filter_cutoff_hz = 11.F;
	rejects(bad, AccelerationFeedbackResult::InvalidMetadata, "applied filter must match captured native intent");
	bad = first; bad.bias_timestamp_sample = start - 100001;
	rejects(bad, AccelerationFeedbackResult::Stale, "fresh bias publication cannot hide old bias sample");
	bad = first; bad.timestamp_sample_us = start - 96000;
	rejects(bad, AccelerationFeedbackResult::Stale, "latency consumes age budget rather than extending deadline");
	bad = first; bad.state.attitude_sample_us = start - 15300;
	rejects(bad, AccelerationFeedbackResult::Skew, "state/raw sample skew plus latency must fit configured skew");
	bad = first; bad.state.valid = false;
	rejects(bad, AccelerationFeedbackResult::StateRejected, "invalid selected state cannot produce feedback");
	bad = first; bad.state.attitude = research_stabilized_manual::Quaternion(1.2F, 0.F, 0.F, 0.F);
	rejects(bad, AccelerationFeedbackResult::StateRejected, "invalid quaternion is not silently normalized");
	bad = first; bad.specific_force_frd_m_s2(0) = NAN;
	rejects(bad, AccelerationFeedbackResult::InvalidInput, "nonfinite specific force preserves destination");
	bad = first; bad.specific_force_frd_m_s2(0) = 200.F;
	rejects(bad, AccelerationFeedbackResult::LimitExceeded, "specific-force magnitude bound is independent of requested acceleration");

	for (unsigned mutation = 0; mutation < 7; ++mutation) {
		AccelerationFeedback probe;
		probe.configure(c);
		probe.evaluate(start, first, output);
		bad = ready;

		switch (mutation) {
		case 0: ++bad.source_generation; break;

		case 1: ++bad.publisher_revision; break;

		case 2: ++bad.raw_publisher_revision; break;

		case 3: ++bad.bias_publisher_revision; break;

		case 4: ++bad.raw_instance; break;

		case 5: ++bad.state.attitude_reset_counter; break;

		default: ++bad.state.source_epoch; break;
		}

		output = sentinel;
		expect(probe.evaluate(ready_time, bad, output) == AccelerationFeedbackResult::SourceChanged && same(output, sentinel),
		       "publisher/generation/reset replacement revokes before accepting new source");
		expect(probe.evaluate(ready_time + 1, bad, output) == AccelerationFeedbackResult::Settling,
		       "replacement begins conservative new settling only on subsequent coherent observation");
	}

	feedback.configure(c);
	feedback.evaluate(start, first, output);
	feedback.evaluate(ready_time, ready, output);
	bad = ready; bad.specific_force_frd_m_s2(0) += 1.F;
	const auto before_mutation = output;
	expect(feedback.evaluate(ready_time + 1, bad, output) == AccelerationFeedbackResult::InvalidInput && same(output, before_mutation),
	       "held raw timestamp cannot mutate filtered measurement");
	auto disabled = c;
	disabled.feedback.enabled = 0;
	expect(!feedback.configure(disabled) && feedback.evaluate(ready_time + 2, ready, output) == AccelerationFeedbackResult::Disabled,
	       "disabled configuration stops prior feedback without changing destination");
	auto invalid = c;
	invalid.feedback.maximum_age_us = invalid.feedback.latency_bound_us;
	expect(!feedback.configure(invalid), "zero effective age is unconfigured");
	invalid = c; invalid.feedback.imu_position[0] = NAN;
	expect(!feedback.configure(invalid), "nonfinite lever arm cannot configure");
	auto low_cutoff = c;
	low_cutoff.feedback.cutoff_hz = 0.5F;
	auto low_first = first, low_ready = ready;
	low_first.filter_cutoff_hz = low_ready.filter_cutoff_hz = 1.F;
	expect(feedback.configure(low_cutoff), "native requested cutoff can be below applied LPF clamp");
	feedback.evaluate(start, low_first, output);
	expect(feedback.evaluate(ready_time, low_ready, output) == AccelerationFeedbackResult::Ready,
	       "actual LPF cutoff clamp is verified rather than comparing parameter intent blindly");
	auto limited = c;
	limited.feedback.maximum_acceleration_m_s2 = 1.F;
	feedback.configure(limited);
	output = sentinel;
	expect(feedback.evaluate(start, first, output) == AccelerationFeedbackResult::LimitExceeded && same(output, sentinel),
	       "CG acceleration magnitude limit independently rejects finite correction");
	return passed;
}
