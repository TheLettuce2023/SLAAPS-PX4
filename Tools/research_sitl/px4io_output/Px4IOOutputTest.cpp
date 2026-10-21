// Narrow production-method fixture: the generated include contains verbatim
// PX4IO::updateOutputs and io_reg_set from the source under test. Only the mixer
// assignment query, device transport and performance/logging hooks are injected.
#include <lib/research_stabilized_manual/StabilizedOutputDriver.hpp>
#include <modules/px4iofirmware/protocol.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using namespace research_stabilized_manual;
namespace
{
constexpr unsigned kIoChannels{8}; // PX4IO::PX4IO_MAX_ACTUATORS on the target.
constexpr int OK{0};
void perf_begin(int) {}
void perf_end(int) {}
#define PX4_DEBUG(...) do {} while (0)

struct Device {
	bool succeed{true};
	int result{-EIO};
	unsigned calls{}, address{}, count{};
	uint16_t values[kIoChannels] {};
	int write(unsigned target, void *data, unsigned size)
	{
		++calls;
		address = target;
		count = size;

		for (unsigned i = 0; i < size && i < kIoChannels; ++i) { values[i] = static_cast<uint16_t *>(data)[i]; }

		return succeed ? static_cast<int>(size) : result;
	}
};
struct Assignments {
	unsigned mask{255};
	bool isFunctionSet(unsigned channel) const { return mask & (1U << channel); }
};
class PX4IO
{
public:
	static constexpr unsigned MAX_ACTUATORS{kIoChannels};
	bool updateOutputs(float outputs[MAX_ACTUATORS], unsigned num_outputs, unsigned num_control_groups_updated);
	int io_reg_set(uint8_t page, uint8_t offset, const uint16_t *values, unsigned num_values);
	Assignments _mixing_output{};
	bool _test_fmu_fail{};
	unsigned _max_transfer{2 * kIoChannels};
	int _interface_write_perf{};
	Device device{};
	Device *_interface{&device};
};

#include "Px4IOOutputMethods.inc"

unsigned checks{}, failures{};
void check(bool ok, const char *name)
{
	++checks;

	if (!ok) { ++failures; printf("FAIL %s\n", name); }
}

void transferTests()
{
	for (unsigned count = 1; count <= kIoChannels; ++count) {
		for (unsigned mask = 0; mask < (1U << kIoChannels); ++mask) {
			PX4IO io;
			io._mixing_output.mask = mask;
			float outputs[kIoChannels] {};

			for (unsigned i = 0; i < count; ++i) { outputs[i] = 500.25F + 100.0F * i + (i % 2 ? 0.5F : 0.0F); }

			check(io.updateOutputs(outputs, count, 1), "full register write handled");
			check(io.device.calls == 1 && io.device.address == (PX4IO_PAGE_DIRECT_PWM << 8)
			      && io.device.count == count, "actual direct-PWM page and register count");

			for (unsigned i = 0; i < count; ++i) {
				const bool assigned = mask & (1U << i);
				const unsigned expected = assigned ? 500 + 100 * i + i % 2 : 0;
				check(io.device.values[i] == expected, "rounding and disabled-channel suppression unchanged");
				check(assigned || std::fabs(outputs[i]) < 0.001F, "disabled output reflected in caller telemetry candidate");
			}
		}
	}

	for (unsigned count = 1; count <= kIoChannels; ++count) {
		// A bus error, zero/short response or an impossible over-count cannot
		// become driver acceptance. io_reg_set is the actual production method.
		for (int returned : {-EIO, -ETIMEDOUT, -EINVAL, 0, static_cast<int>(count) - 1, static_cast<int>(count) + 1}) {
			PX4IO io;
			io.device.succeed = false;
			io.device.result = returned;
			float outputs[kIoChannels] {};
			check(!io.updateOutputs(outputs, count, 0), "transport failure must not report handled");
			check(io.device.calls == 1, "failed transfer attempted exactly once at driver boundary");
		}

		PX4IO limited;
		limited._max_transfer = 2 * (count - 1);
		float outputs[kIoChannels] {};
		check(!limited.updateOutputs(outputs, count, 1) && limited.device.calls == 0, "oversized register transfer never acknowledged");
		PX4IO suppressed;
		suppressed._test_fmu_fail = true;
		check(!suppressed.updateOutputs(outputs, count, 1) && suppressed.device.calls == 0, "FMU-failure injection cannot report output handled");
	}
}

OutputConfiguration configuration()
{
	OutputConfiguration c{};
	c.lease_us = 20000;
	c.acknowledgement_us = 8000;
	c.on_fault = OutputFallback::Disarmed;

	for (unsigned i = 0; i < 4; ++i) {
		c.servos[i] = {1100.0F, 1500.0F, 1900.0F, 500, 1500, 2500};
		c.minimum[i] = -1.0F;
		c.maximum[i] = 1.0F;
	}

	return c;
}

struct Sink : StabilizedOutputSink {
	PX4IO io{};
	TimeUs now{1000000};
	OutputWriteResult write(const OutputCandidate &candidate) override
	{
		float outputs[kIoChannels] {};

		if (candidate.disposition != OutputDisposition::Disarmed) {
			for (unsigned i = 0; i < 4; ++i) { outputs[i] = candidate.pwm_us[i]; }
		}

		return {now, io.updateOutputs(outputs, 4, 1)};
	}
};

void acknowledgementTests()
{
	for (unsigned failure = 0; failure < 4; ++failure) {
		StabilizedOutputDriver driver;
		Sink sink;
		check(driver.dispatch(sink.now, true, nullptr, sink).revoke_acknowledged, "initial disarm really reaches bus");
		check(driver.begin(sink.now, sink.now, configuration(), true) == OutputStatus::Ok, "grant after bus-handled disarm");
		OutputFrame frame{sink.now, sink.now + 4000, 1, {0.5F, -0.5F, 0.25F, -0.25F}};
		sink.now = frame.timestamp_us;

		if (failure == 0) { sink.io.device.succeed = false; sink.io.device.result = -EIO; }

		if (failure == 1) { sink.io.device.succeed = false; sink.io.device.result = 3; }

		if (failure == 2) { sink.io._test_fmu_fail = true; }

		if (failure == 3) { sink.io._max_transfer = 6; }

		const auto rejected = driver.dispatch(sink.now, true, &frame, sink);
		check(!rejected.handled && !rejected.command_acknowledged && driver.state() == DriverState::Fault,
		      "real IO failure suppresses stabilized command acknowledgement");
		driver.revoke();
		const auto pending = driver.dispatch(sink.now, true, nullptr, sink);
		check(!pending.revoke_acknowledged && !driver.disarmedConfirmed(), "failed disarm cannot release output ownership");
		sink.io.device.succeed = true;
		sink.io._test_fmu_fail = false;
		sink.io._max_transfer = 2 * kIoChannels;
		++sink.now;
		check(driver.dispatch(sink.now, true, nullptr, sink).revoke_acknowledged, "bus recovery confirms requested disarm");
		check(driver.begin(sink.now, sink.now, configuration(), true) == OutputStatus::Ok, "fresh grant only after recovery");
		frame.session = sink.now;
		frame.timestamp_us = sink.now += 4000;
		const auto handled = driver.dispatch(sink.now, true, &frame, sink);
		check(handled.handled && handled.command_acknowledged, "real successful IO write permits software ack");
		check(sink.io.device.values[0] == 1700 && sink.io.device.values[1] == 1300
		      && sink.io.device.values[2] == 1600 && sink.io.device.values[3] == 1400,
		      "all four calibrated registers reach injected transport");
	}
}
}

int main()
{
	transferTests();
	acknowledgementTests();
	printf("PX4IO_OUTPUT_RESULT checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
