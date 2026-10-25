#pragma once

#include <uORB/ResearchSubscription.hpp>

namespace research_stabilized_manual
{

using PublisherSubscription = uORB::ResearchSubscription;

class PublisherBinding
{
public:
	// Attitude, angular velocity, estimator status, estimator flags, manual
	// input, sensor selection and actuator permission, in that order.
	static constexpr unsigned kTopics{7};
	static constexpr unsigned kManualTopic{4};
	static constexpr unsigned kAllTopicsMask{(1U << kTopics) - 1U};
	static constexpr unsigned kStateTopicsMask{kAllTopicsMask & ~(1U << kManualTopic)};
	uint32_t observe(const unsigned(&revisions)[kTopics], unsigned required_topics = kAllTopicsMask)
	{
		if (!required_topics || (required_topics & ~kAllTopicsMask)) { return 0; }

		bool changed = required_topics != _required_topics;

		for (unsigned i = 0; i < kTopics; ++i) {
			if (!(required_topics & (1U << i))) { continue; }

			if (!revisions[i]) { return 0; }

			changed = changed || revisions[i] != _revisions[i];
		}

		if (!_bound) { _epoch = 1; _bound = true; }

		else if (changed && _epoch) { _epoch = _epoch == UINT32_MAX ? 0 : _epoch + 1; }

		for (unsigned i = 0; i < kTopics; ++i) { _revisions[i] = required_topics & (1U << i) ? revisions[i] : 0; }

		_required_topics = required_topics;

		return _epoch;
	}
private:
	unsigned _revisions[kTopics] {};
	unsigned _required_topics{};
	uint32_t _epoch{};
	bool _bound{};
};

} // namespace research_stabilized_manual
