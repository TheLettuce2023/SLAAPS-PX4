#pragma once

#include "BenchControlOwner.hpp"
#include "Px4ManualControlInput.hpp"

namespace research_actuator_command
{

// Inert scheduling/subscription adapter, never instantiated by startup. All
// lifecycle calls, reads and destruction must be serialized with Run() by the
// enclosing PX4 owner. No publisher, driver access, arming or output callback.
class Px4BenchControlRuntime final : public px4::ScheduledWorkItem
{
public:
	Px4BenchControlRuntime(const char *name, const px4::wq_config_t &work_queue);
	~Px4BenchControlRuntime() override;
	Px4BenchControlRuntime(const Px4BenchControlRuntime &) = delete;
	Px4BenchControlRuntime &operator=(const Px4BenchControlRuntime &) = delete;
	Px4BenchControlRuntime(Px4BenchControlRuntime &&) = delete;
	Px4BenchControlRuntime &operator=(Px4BenchControlRuntime &&) = delete;

	BenchStatus configure(const BenchConfiguration &configuration);
	BenchStatus enable();
	void disable();
	void reset();
	const BenchControlOwner &owner() const { return _owner; }

private:
	void Run() override;
	ManualControlSample readSample();
	BenchControlOwner _owner{};
	uORB::Subscription _manual_sub{ORB_ID(manual_control_setpoint)};
};

} // namespace research_actuator_command
