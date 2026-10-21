#pragma once

#include "../Common.hpp"
#include <parameters/param.h>
#include <uORB/Subscription.hpp>
#include <uORB/topics/slaaps_status.h>

class SlaapsChecks : public HealthAndArmingCheckBase
{
public:
	void checkAndReport(const Context &context, Report &reporter) override;
private:
	uORB::Subscription _status{ORB_ID(slaaps_status)};
	const param_t _autostart{param_find("SYS_AUTOSTART")};
	bool _slaaps_airframe{};
};
