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
	uint32_t observe(const unsigned(&revisions)[kTopics])
	{
		bool changed = false;

		for (unsigned i = 0; i < kTopics; ++i) {
			if (!revisions[i]) { return 0; }

			changed = changed || revisions[i] != _revisions[i];
		}

		if (!_bound) { _epoch = 1; _bound = true; }

		else if (changed && _epoch) { _epoch = _epoch == UINT32_MAX ? 0 : _epoch + 1; }

		for (unsigned i = 0; i < kTopics; ++i) { _revisions[i] = revisions[i]; }

		return _epoch;
	}
private:
	unsigned _revisions[kTopics] {};
	uint32_t _epoch{};
	bool _bound{};
};

} // namespace research_stabilized_manual
