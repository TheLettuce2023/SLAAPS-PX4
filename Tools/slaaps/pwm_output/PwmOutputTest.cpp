#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
constexpr unsigned kChannels = 8;
unsigned calls{}, triggers{}, failed_channel = kChannels;
uint16_t values[kChannels] {};
unsigned written_mask{};

int up_pwm_servo_set(unsigned channel, uint16_t value)
{
	++calls;

	if (channel >= kChannels) { return -1; }

	written_mask |= 1U << channel;
	values[channel] = value;
	return channel == failed_channel ? -1 : 0;
}
void up_pwm_update(uint32_t) { ++triggers; }

struct Assignments {
	unsigned mask{255};
	bool isFunctionSet(unsigned channel) const { return mask & (1U << channel); }
};
class PWMOut
{
public:
	static constexpr unsigned MAX_ACTUATORS = kChannels;
	bool updateOutputs(float outputs[MAX_ACTUATORS], unsigned num_outputs, unsigned num_control_groups_updated);
	bool _pwm_initialized{};
	uint32_t _pwm_mask{255};
	Assignments _mixing_output;
};
#include "PwmOutputMethod.inc"

unsigned checks{}, failures{};
void check(bool condition, const char *name)
{
	++checks;

	if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
}
void reset()
{
	calls = triggers = written_mask = 0;
	failed_channel = kChannels;

	for (auto &value : values) { value = 0; }
}
}

int main()
{
	PWMOut driver;
	float outputs[kChannels] {1000.F, 1100.4F, 1200.6F, 1300.F, 1400.F, 1500.F, 1600.F, 1700.F};
	reset();
	check(!driver.updateOutputs(outputs, kChannels, 0), "uninitialized PWM must not acknowledge handling");
	check(calls == 0, "uninitialized driver performs no channel writes");
	driver._pwm_initialized = true;
	check(driver.updateOutputs(outputs, kChannels, 1), "all successful register writes handled");
	check(calls == kChannels && written_mask == 255 && triggers == 1, "every hardware channel and update reached");
	check(values[1] == 1100 && values[2] == 1201 && values[7] == 1700, "actual driver rounding preserved");

	for (unsigned channel = 0; channel < kChannels; ++channel) {
		reset();
		failed_channel = channel;
		check(!driver.updateOutputs(outputs, kChannels, 1), "any channel error rejects bank handling");
		check(calls == kChannels && written_mask == 255 && triggers == 1, "failure does not skip remaining register writes");
	}

	reset();
	driver._mixing_output.mask &= ~(1U << 3);
	check(driver.updateOutputs(outputs, kChannels, 0) && values[3] == 0, "disabled function writes no pulse");
	check(triggers == 0, "no fresh control groups preserves ordinary PWM trigger behavior");
	reset();
	driver._pwm_mask = 0x55;
	check(driver.updateOutputs(outputs, kChannels, 1) && calls == 4 && written_mask == 0x55, "only initialized hardware mask written");
	reset();
	failed_channel = 7;
	check(driver.updateOutputs(outputs, kChannels, 1), "unselected hardware channel is not attempted");
	std::printf("PWM_OUTPUT_RESULT checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
