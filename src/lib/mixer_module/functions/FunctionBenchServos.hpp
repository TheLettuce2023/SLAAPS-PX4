#pragma once

#include "FunctionProviderBase.hpp"
#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <uORB/topics/bench_servo_command.h>
#include <drivers/drv_hrt.h>

class FunctionBenchServos : public FunctionProviderBase
{
public:
	explicit FunctionBenchServos(const Context &context) : _topic(&context.work_item, ORB_ID(bench_servo_command)) {}
	static FunctionProviderBase *allocate(const Context &context) { return new FunctionBenchServos(context); }
	void update() override
	{
		bench_servo_command_s message{};

		if (_topic.update(&message)) {
			research_actuator_command::BenchOutputFrame frame{};
			frame.timestamp = message.timestamp;
			frame.session = message.session;
			frame.lease_us = message.lease_us;
			frame.enabled = message.enabled;

			for (unsigned i = 0; i < 4; ++i) { frame.control[i] = message.control[i]; }

			_lease.update(hrt_absolute_time(), frame);
		}
	}
	float value(OutputFunction func) override
	{
		return _lease.value(hrt_absolute_time(), static_cast<unsigned>(func) - static_cast<unsigned>(OutputFunction::Research_Bench1));
	}
	float defaultFailsafeValue(OutputFunction) const override { return NAN; }
	void revoke() { _lease.revoke(); }
	uORB::SubscriptionCallbackWorkItem *subscriptionCallback() override { return &_topic; }
private:
	uORB::SubscriptionCallbackWorkItem _topic;
	research_actuator_command::BenchOutputLease _lease{};
};
