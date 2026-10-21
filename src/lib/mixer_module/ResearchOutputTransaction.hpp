#pragma once

#include <drivers/drv_hrt.h>

struct actuator_armed_s;

// Optional guard for an exclusively owned MAIN PWM bank. Install before the
// first mixer update and keep alive until the mixer is destroyed. All calls
// execute on the driver's serialized work-queue context, including completion.
// This is not a publisher, authority grant, or physical-output acknowledgment.
class ResearchOutputTransaction
{
public:
	virtual ~ResearchOutputTransaction() = default;
	// Only a prepared candidate supplies normalized canard controls. NAN means
	// disarmed; no shared actuator topic supplies this bank's research values.
	virtual float control(unsigned channel) const = 0;
	// Validate the entire final bank, after limits/arming/test overrides but
	// before the physical driver. Rejection substitutes zero PWM on every
	// channel; completion reports false even if that substitute write succeeds.
	virtual bool validate(const float *outputs, unsigned count, const actuator_armed_s &armed) = 0;
	// Always called after an attempted driver write. The clock is sampled after
	// the driver returns. False means the prepared candidate was not handled.
	virtual void complete(bool handled, hrt_abstime completed) = 0;
};
