#pragma once

#include <px4_platform_common/px4_config.h>

#if !defined(CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL) && !defined(CONFIG_MODULES_SLAAPS)
#error Research subscription requires a research or SLAAPS-enabled uORB build.
#endif
#if defined(__PX4_NUTTX) && !defined(CONFIG_BUILD_FLAT)
#error Direct research subscriptions are supported only in flat NuttX builds.
#endif

#include <uORB/Subscription.hpp>
#include <uORB/uORBDeviceNode.hpp>

namespace uORB
{

// Payload and advertisement lifetime are read in the same uORB critical
// section. This is lifecycle metadata, not authentication of publisher handles.
class ResearchSubscription : public Subscription
{
public:
	using Subscription::Subscription;
	bool copyBound(void *destination, unsigned &revision)
	{
		revision = 0;
		return subscribe()
		       && static_cast<DeviceNode *>(get_node())->copy_research_revision(destination, _last_generation, revision);
	}
	// Consume one unread queued sample from the current advertised lifetime.
	// No new data returns false; unlike copyBound, this never repeats a sample.
	bool copyQueuedBound(void *destination, unsigned &revision)
	{
		revision = 0;
		return subscribe()
		       && static_cast<DeviceNode *>(get_node())->copy_research_queued_revision(destination, _last_generation, revision);
	}
	unsigned revision()
	{
		return subscribe() ? static_cast<DeviceNode *>(get_node())->research_revision() : 0;
	}
};

} // namespace uORB
