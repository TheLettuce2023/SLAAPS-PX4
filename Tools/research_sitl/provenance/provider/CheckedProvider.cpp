#include "CheckedProvider.hpp"

#include <cerrno>
#include <limits>
#include <time.h>

namespace research_sitl::clock_observation
{
namespace
{
constexpr uint64_t MicrosecondsPerSecond = 1000000;
constexpr int64_t NanosecondsPerSecond = 1000000000;
constexpr int64_t NanosecondsPerMicrosecond = 1000;
bool equal(const Binding &a, const Binding &b)
{
	return a.run == b.run && a.owner == b.owner && a.controller == b.controller && a.producer == b.producer
	       && a.provider == b.provider && a.clock == b.clock && a.session == b.session && a.epoch == b.epoch;
}
}

Reading convert(int result, int error, int64_t seconds, int64_t nanoseconds) noexcept
{
	Reading value{};
	value.seconds = seconds;
	value.nanoseconds = nanoseconds;

	if (result != 0) { value.error = error; return value; }

	if (seconds < 0 || nanoseconds < 0 || nanoseconds >= NanosecondsPerSecond) {
		value.code = ReadCode::InvalidTimespec; return value;
	}

	const uint64_t fraction = static_cast<uint64_t>(nanoseconds / NanosecondsPerMicrosecond);

	if (static_cast<uint64_t>(seconds) > (UINT64_MAX - fraction) / MicrosecondsPerSecond) {
		value.code = ReadCode::Overflow; return value;
	}

	value.microseconds = static_cast<uint64_t>(seconds) * MicrosecondsPerSecond + fraction;
	value.code = value.microseconds ? ReadCode::Ok : ReadCode::Zero;
	return value;
}

Reading readMonotonic() noexcept
{
	static_assert(std::numeric_limits<time_t>::is_signed && sizeof(time_t) <= sizeof(int64_t));
	timespec time{};
	errno = 0;
	const int result = clock_gettime(CLOCK_MONOTONIC, &time);
	const int error = errno; // Capture before any subsequent library call.
	return convert(result, result == 0 ? 0 : error, time.tv_sec, time.tv_nsec);
}

bool CheckedProvider::fail(Fault fault, Reading reading) noexcept
{
	if (state_ != State::Lost) {
		terminal_ = {fault, state_, reading, count_};
		state_ = State::Lost;
	}

	return false;
}

bool CheckedProvider::valid(const Binding &binding, Interval interval) noexcept
{
	if (!binding.run || !binding.owner || !binding.controller || !binding.producer || !binding.provider
	    || !binding.clock || !binding.session || !binding.epoch) { return fail(Fault::Binding); }

	// +/-1 is candidate rounding arithmetic, NOT an approved uncertainty policy.
	if (interval.first <= 1 || interval.last < interval.first || interval.last == UINT64_MAX) { return fail(Fault::Interval); }

	return true;
}

bool CheckedProvider::readValid(Reading reading) noexcept
{
	if (reading.code != ReadCode::Ok) { return fail(Fault::Read, reading); }

	if (count_ && (reading.seconds < last_read_.seconds ||
		       (reading.seconds == last_read_.seconds && reading.nanoseconds < last_read_.nanoseconds))) {
		return fail(Fault::Regression, reading);
	}

	return true;
}

bool CheckedProvider::append(Kind kind, Reading reading, uint64_t previous_epoch, uint64_t previous_closure) noexcept
{
	if (count_ == EventCapacity) { return fail(Fault::Capacity, reading); }

	events_[count_] = {count_ + 1, kind, binding_, interval_, reading, previous_epoch, previous_closure};
	++count_;
	last_read_ = reading;
	return true;
}

bool CheckedProvider::beginRead(const Binding &binding, Interval interval, Reading reading) noexcept
{
	if (state_ == State::Lost) { return false; }

	if (state_ != State::Empty) { return fail(Fault::Order); }

	if (!valid(binding, interval) || !readValid(reading)) { return false; }

	if (reading.microseconds < interval.first) { return fail(Fault::BeforeInterval, reading); }

	if (reading.microseconds > interval.last) { return fail(Fault::AfterInterval, reading); }

	binding_ = binding;
	interval_ = interval;

	if (!append(Kind::Begin, reading)) { return false; }

	state_ = State::Active;
	return true;
}

bool CheckedProvider::pollRead(Reading reading, bool retiring) noexcept
{
	if (state_ == State::Lost) { return false; }

	if (state_ != State::Active) { return fail(Fault::Order); }

	if (!readValid(reading)) { return false; }

	const bool expired = reading.microseconds > interval_.last;
	const Kind kind = expired ? Kind::Expired : (retiring ? Kind::Retired : Kind::Observation);

	if (!append(kind, reading)) { return false; }

	if (expired || retiring) {
		closure_sequence_ = count_;
		state_ = expired ? State::Expired : State::Retired;
	}

	return !expired;
}

bool CheckedProvider::replaceRead(const ReplacementKey &previous, const Binding &binding, Interval interval,
				  Reading reading) noexcept
{
	if (state_ == State::Lost) { return false; }

	if ((state_ != State::Expired && state_ != State::Retired) || previous.closure_sequence != closure_sequence_
	    || !equal(previous.binding, binding_) || previous.interval.first != interval_.first
	    || previous.interval.last != interval_.last) { return fail(Fault::Order); }

	Binding expected = binding_;
	expected.epoch = binding.epoch;
	expected.session = binding.session;

	if (!valid(binding, interval)) { return false; }

	if (!equal(binding, expected) || binding.epoch <= binding_.epoch || binding.session == binding_.session) { return fail(Fault::Binding); }

	if (!readValid(reading)) { return false; }

	if (reading.microseconds < interval.first) { return fail(Fault::BeforeInterval, reading); }

	if (reading.microseconds > interval.last) { return fail(Fault::AfterInterval, reading); }

	// Reserve predecessor/successor relationship before changing the active candidate.
	if (count_ == EventCapacity) { return fail(Fault::Capacity, reading); }

	const uint64_t old_epoch = binding_.epoch;
	const uint64_t old_closure = closure_sequence_;
	binding_ = binding;
	interval_ = interval;

	if (!append(Kind::Replacement, reading, old_epoch, old_closure)) { return false; }

	closure_sequence_ = 0;
	state_ = State::Active;
	return true;
}

bool CheckedProvider::begin(const Binding &binding, Interval interval) noexcept { return beginRead(binding, interval, readMonotonic()); }
bool CheckedProvider::poll() noexcept { return pollRead(readMonotonic(), false); }
bool CheckedProvider::retire() noexcept { return pollRead(readMonotonic(), true); }
bool CheckedProvider::replace(const ReplacementKey &previous, const Binding &binding, Interval interval) noexcept
{
	return replaceRead(previous, binding, interval, readMonotonic());
}
bool CheckedProvider::copy(size_t index, Event &out) const noexcept
{
	if (index >= count_) { return false; }

	out = events_[index];
	return true;
}
}
