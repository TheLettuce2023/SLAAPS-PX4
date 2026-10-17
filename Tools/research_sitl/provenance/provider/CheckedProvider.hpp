#pragma once

#if !defined(RESEARCH_CLOSED_CLOCK_HOST) || RESEARCH_CLOSED_CLOCK_HOST != 1 || !defined(__linux__) \
    || defined(__PX4_POSIX) || defined(__PX4_NUTTX) || defined(__PX4_LINUX) || defined(__PX4_ROS2) \
    || defined(__NuttX__) || defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) \
    || defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
    || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Closed Linux host clock observation only"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace research_sitl::clock_observation
{
enum class ReadCode : uint8_t { Ok, SystemError, InvalidTimespec, Overflow, Zero };
struct Reading {
	ReadCode code{ReadCode::SystemError};
	int error{};
	int64_t seconds{}, nanoseconds{};
	uint64_t microseconds{};
};
// Pure representation check; cannot establish provider identity or trust.
Reading convert(int result, int error, int64_t seconds, int64_t nanoseconds) noexcept;
Reading readMonotonic() noexcept;

struct Binding {
	uint64_t run{}, owner{}, controller{}, producer{}, provider{}, clock{}, session{}, epoch{};
};
struct Interval { uint64_t first{}, last{}; };
enum class State : uint8_t { Empty, Active, Expired, Retired, Lost };
enum class Kind : uint8_t { Begin, Observation, Expired, Retired, Replacement };
enum class Fault : uint8_t { None, Order, Binding, Interval, Read, Regression, BeforeInterval, AfterInterval, Capacity };
struct Event {
	uint64_t sequence{};
	Kind kind{};
	Binding binding{};
	Interval interval{};
	Reading reading{};
	uint64_t predecessor_epoch{}, predecessor_closure{};
};
struct ReplacementKey {
	Binding binding{};
	Interval interval{};
	uint64_t closure_sequence{};
};
struct Terminal {
	Fault fault{Fault::None};
	State previous_state{State::Empty};
	Reading reading{}; // Failed read may have no timestamp; never reuse a success time.
	uint64_t last_event_sequence{};
};
constexpr size_t EventCapacity = 64;

// One serialized host owner. No adapter, map, readiness, packet or qualification API.
class CheckedProvider final
{
public:
	CheckedProvider() = default;
	CheckedProvider(const CheckedProvider &) = delete;
	CheckedProvider &operator=(const CheckedProvider &) = delete;
	CheckedProvider(CheckedProvider &&) = delete;
	CheckedProvider &operator=(CheckedProvider &&) = delete;
	bool begin(const Binding &binding, Interval interval) noexcept;
	bool poll() noexcept;
	bool retire() noexcept;
	bool replace(const ReplacementKey &previous, const Binding &binding, Interval interval) noexcept;
	State state() const noexcept { return state_; }
	size_t count() const noexcept { return count_; }
	bool copy(size_t index, Event &out) const noexcept;
	ReplacementKey replacementKey() const noexcept { return {binding_, interval_, closure_sequence_}; }
	Terminal terminal() const noexcept { return terminal_; }
private:
	friend struct ProviderTestPeer;
	bool beginRead(const Binding &, Interval, Reading) noexcept;
	bool pollRead(Reading, bool retire) noexcept;
	bool replaceRead(const ReplacementKey &, const Binding &, Interval, Reading) noexcept;
	bool valid(const Binding &, Interval) noexcept;
	bool readValid(Reading) noexcept;
	bool append(Kind, Reading, uint64_t previous_epoch = 0, uint64_t previous_closure = 0) noexcept;
	bool fail(Fault, Reading = {}) noexcept;
	std::array<Event, EventCapacity> events_{};
	Binding binding_{};
	Interval interval_{};
	Reading last_read_{};
	size_t count_{};
	uint64_t closure_sequence_{};
	State state_{State::Empty};
	Terminal terminal_{}; // Reserved independently of ordinary event capacity.
};
}
