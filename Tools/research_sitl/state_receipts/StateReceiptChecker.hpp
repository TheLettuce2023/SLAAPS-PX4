#pragma once

#if !defined(RESEARCH_SITL_STATE_RECEIPT_HOST_TEST) || RESEARCH_SITL_STATE_RECEIPT_HOST_TEST != 1
#error "State receipts require the isolated standalone host-test target"
#endif
#if !defined(__linux__) || defined(__PX4_NUTTX) || defined(__PX4_POSIX) || defined(__PX4_LINUX) \
	|| defined(__PX4_ROS2) || defined(__NuttX__) || defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "State receipts are not released for PX4, hardware, lockstep or timestamp proxies"
#endif

#include <cstdint>

namespace research_sitl
{
namespace state_receipts
{

struct State {
	uint8_t arming_state{};
	uint8_t nav_state{};
};

enum class ReceiptKind : uint8_t { Initial, Transition, Sample };

struct Receipt {
	uint64_t producer_lifetime_id{};
	uint64_t clock_epoch{};
	uint64_t sequence{};
	uint64_t transition_epoch{};
	uint64_t producer_time_us{};
	ReceiptKind kind{};
	State before{};
	State after{};
};

struct Observation {
	uint64_t observer_lifetime_id{};
	uint64_t receive_time_us{};
	bool clock_qualified{};
	Receipt receipt{};
};

struct Config {
	uint64_t expected_producer_lifetime_id{};
	uint64_t expected_observer_lifetime_id{};
	uint64_t expected_clock_epoch{};
	uint64_t max_age_us{}; // Explicit synthetic fixture bound; zero is invalid, not an operational default.
	uint32_t allowed_nav_mask{};
};

enum class Code : uint8_t { AwaitingInitial, Fresh, Stale, Faulted };

enum class FaultReason : uint8_t {
	None,
	InvalidConfig,
	ObserverLifetimeMismatch,
	ProducerLifetimeMismatch,
	ClockEpochMismatch,
	ClockUnqualified,
	InvalidKind,
	InvalidState,
	MissingInitial,
	RepeatedInitial,
	ReceiptContinuity,
	StateContradiction,
	TransitionEpochContradiction,
	TimeInvalid,
	TimeRegressed,
	CounterExhausted
};

struct Snapshot {
	Code code{};
	FaultReason first_fault{};
	bool has_state{};
	uint64_t receipt_sequence{};
	uint64_t transition_epoch{};
	State state{};
	uint64_t producer_time_us{};
	uint64_t local_time_us{};
	bool age_valid{};
	uint64_t age_us{};
};

// Structural provenance only. Fresh never grants admission or qualifies any
// actual producer hook, estimator, clock map, research selection or live budget.
class ReceiptChecker
{
public:
	explicit ReceiptChecker(const Config &config) noexcept;
	ReceiptChecker(const ReceiptChecker &) = delete;
	ReceiptChecker &operator=(const ReceiptChecker &) = delete;
	ReceiptChecker(ReceiptChecker &&) = delete;
	ReceiptChecker &operator=(ReceiptChecker &&) = delete;

	Snapshot observe(const Observation &observation) noexcept;
	Snapshot poll(uint64_t observer_lifetime_id, uint64_t clock_epoch,
		      uint64_t now_us, bool clock_qualified) noexcept;

private:
	friend struct StateReceiptCheckerTestPeer;
	bool validState(const State &state) const noexcept;
	FaultReason localTimeFault(uint64_t now_us) const noexcept;
	Snapshot fail(FaultReason reason) noexcept;
	Snapshot commitTime(uint64_t now_us) noexcept;

	const Config config_;
	Snapshot snapshot_{};
};

} // namespace state_receipts
} // namespace research_sitl
