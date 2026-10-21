#pragma once

#include <uORB/ResearchSubscription.hpp>

namespace slaaps
{

enum class BoundRead : uint8_t { Data, Waiting, Rejected };

// The supervisor supplies the expected advertisement lifetime at admission.
// A payload cannot bind itself. This detects lifecycle changes, not malicious
// publishers sharing a uORB handle. Session/permission validation is separate.
template<typename T>
class BoundSubscription
{
public:
	explicit BoundSubscription(ORB_ID topic) : _subscription(topic) {}
	BoundSubscription(const BoundSubscription &) = delete;
	BoundSubscription &operator=(const BoundSubscription &) = delete;

	bool bind(unsigned expected_revision)
	{
		_revision = expected_revision;
		_valid = expected_revision && _subscription.get_topic()
			 && _subscription.get_topic()->o_size == sizeof(T)
			 && _subscription.get_topic()->o_queue == 1
			 && _subscription.revision() == expected_revision;
		return _valid;
	}

	// Waiting is possible only before this advertisement publishes its first
	// payload. A lifetime change still latches rejection. Waiting grants no data.
	BoundRead read(T &destination)
	{
		if (!_valid) { return BoundRead::Rejected; }

		T candidate{};
		unsigned revision{};

		if (!_subscription.copyBound(&candidate, revision)) {
			if (_subscription.revision() == _revision) { return BoundRead::Waiting; }

			_valid = false;
			return BoundRead::Rejected;
		}

		if (revision != _revision) {
			_valid = false;
			return BoundRead::Rejected;
		}

		destination = candidate;
		return BoundRead::Data;
	}

	// Strict compatibility interface: missing data also invalidates the binding.
	bool copy(T &destination)
	{
		if (read(destination) == BoundRead::Data) { return true; }

		_valid = false;
		return false;
	}

	void revoke() { _valid = false; }
	bool valid() const { return _valid; }

private:
	uORB::ResearchSubscription _subscription;
	unsigned _revision{};
	bool _valid{};
};

} // namespace slaaps
