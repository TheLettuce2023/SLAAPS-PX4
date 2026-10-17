#pragma once

#if !defined(RESEARCH_STABILIZED_PUBLISHER_EPOCH) || !defined(__PX4_POSIX)
#error This subscription requires the private POSIX uORB lifecycle variant.
#endif

#include <uORB/Subscription.hpp>
#include <uORB/uORBDeviceNode.hpp>
#include <cstdint>
#include <limits>

namespace research_stabilized_epoch
{

// Direct node access is deliberately restricted to this private POSIX build.
// The node lock binds the advertisement revision to the copied queue-one data.
class PublisherRevisionSubscription : public uORB::Subscription
{
public:

	using uORB::Subscription::Subscription;
	using uORB::Subscription::copy;

	bool copyRevision(void *destination, unsigned &revision)
	{
		revision = 0;

		if (!subscribe()) { return false; }

		return static_cast<uORB::DeviceNode *>(get_node())->research_copy_revision(destination, _last_generation, revision);
	}

	unsigned revision()
	{
		return subscribe() ? static_cast<uORB::DeviceNode *>(get_node())->research_advertisement_revision() : 0;
	}

	// Synthetic fault injection only: the existing producer is not stopped.
	// No subscription copy occurs during the withdrawn interval.
	template<typename Message>
	bool cycleAdvertisement(Message &message, unsigned &before, unsigned &after)
	{
		if (!copyRevision(&message, before)) { return false; }

		void *node = get_node();

		if (orb_unadvertise(node) != PX4_OK) { return false; }

		const orb_advert_t replacement = orb_advertise(get_topic(), &message);
		after = revision();
		return replacement == node && after > before;
	}
};

// Map the complete four-topic revision tuple to a process-local core epoch.
// No tuple hashing, collisions or silent wrap to a previously admitted value.
class PublisherRevisionBinding
{
public:
	static constexpr unsigned kTopicCount = 4;

	uint32_t observe(const unsigned(&revisions)[kTopicCount])
	{
		bool changed = false;

		for (unsigned i = 0; i < kTopicCount; ++i) {
			if (revisions[i] == 0) { return 0; }

			changed = changed || revisions[i] != _revisions[i];
		}

		if (!_bound) {
			_epoch = 1;
			_bound = true;

		} else if (changed && _epoch != 0) {
			_epoch = _epoch == std::numeric_limits<uint32_t>::max() ? 0 : _epoch + 1;
		}

		for (unsigned i = 0; i < kTopicCount; ++i) { _revisions[i] = revisions[i]; }

		return _epoch;
	}

private:
	unsigned _revisions[kTopicCount] {};
	uint32_t _epoch{};
	bool _bound{};
};

int publisherRevisionSelfTest();

} // namespace research_stabilized_epoch
