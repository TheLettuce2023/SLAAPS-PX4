#pragma once

#if defined(RESEARCH_SITL_STATE_CAPTURE_HOST_TEST)
#if RESEARCH_SITL_STATE_CAPTURE_HOST_TEST != 1 || !defined(__linux__) || defined(__PX4_POSIX) \
	|| defined(__PX4_NUTTX) || defined(__PX4_LINUX) || defined(__PX4_ROS2) || defined(__NuttX__) \
	|| defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) || defined(ENABLE_LOCKSTEP_SCHEDULER) \
	|| defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Capture helper host tests require isolated Linux"
#endif
#else
#include <px4_platform_common/px4_config.h>
#if !defined(RESEARCH_C1C_CAPTURE) || RESEARCH_C1C_CAPTURE != 1 || !defined(__linux__) \
	|| !defined(__PX4_POSIX) || !defined(CONFIG_ARCH_BOARD_PX4_SITL) || defined(__PX4_NUTTX) \
	|| defined(__PX4_ROS2) || defined(__NuttX__) || defined(ENABLE_LOCKSTEP_SCHEDULER) \
	|| defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Capture helper requires isolated native Linux POSIX PX4 SITL"
#endif
#endif

#include <cstdint>
#include <mutex>
#include <optional>

namespace research_sitl
{
namespace state_capture
{

struct State {
	uint8_t arming_state{};
	uint8_t nav_state{};
};

struct Identities {
	uint64_t producer_lifetime_id{};
	uint64_t observer_lifetime_id{};
	uint64_t clock_epoch{};
};

enum class Kind : uint8_t { Initial, Transition, Sample };

enum class Fault : uint8_t {
	None,
	InvalidIdentity,
	BootstrapUnavailable,
	AlreadyStarted,
	InvalidState,
	TimeInvalid,
	TimeRegressed,
	StateContradiction,
	CounterExhausted,
	PublishFailed,
	PublicationPending
};

struct Receipt {
	uint64_t producer_lifetime_id{};
	uint64_t clock_epoch{};
	uint64_t sequence{};
	uint64_t transition_epoch{};
	uint64_t producer_time_us{};
	Kind kind{};
	State before{};
	State after{};
};

struct Summary {
	Identities identities{};
	uint64_t attempted_sequence{};
	uint64_t emitted_sequence{};
	uint64_t transition_epoch{};
	uint64_t producer_time_us{};
	uint64_t final_time_us{};
	State retained_state{}; // Last attempted receipt, potentially not delivered.
	State emitted_state{};
	uint64_t emitted_transition_epoch{};
	uint64_t emitted_producer_time_us{};
	State actual_state{}; // Direct final observation, never reconstructed from delivery.
	bool has_state{};
	bool finished{};
	Fault first_fault{};
};

// Only bootstrap crosses threads; producer state stays in the Commander thread.
// A repeated configure/claim permanently invalidates the single reserved lifetime.
class Bootstrap
{
public:
	bool configure(const Identities &identities) noexcept;
	bool claim(Identities &identities) noexcept;
	bool healthy() const noexcept;
	Bootstrap() = default;
	Bootstrap(const Bootstrap &) = delete;
	Bootstrap &operator=(const Bootstrap &) = delete;

private:
	mutable std::mutex mutex_;
	Identities identities_{};
	bool configured_{};
	bool claimed_{};
	bool faulted_{};
};

class Producer
{
public:
	explicit Producer(Bootstrap &bootstrap) noexcept : bootstrap_(bootstrap) {}
	Producer(const Producer &) = delete;
	Producer &operator=(const Producer &) = delete;

	std::optional<Receipt> initial(State actual, uint64_t now_us) noexcept;
	std::optional<Receipt> transition(State before, State after, uint64_t now_us) noexcept;
	std::optional<Receipt> sample(State actual, uint64_t now_us) noexcept;
	void completePublication(bool success) noexcept;
	Summary finish(State actual, uint64_t now_us) noexcept;
	Summary snapshot() const noexcept { return summary_; }

private:
	friend struct StateCaptureProducerTestPeer;
	bool active() noexcept;
	bool checkTime(uint64_t now_us) noexcept;
	void fail(Fault fault) noexcept;
	std::optional<Receipt> prepare(Kind kind, State before, State after, uint64_t now_us) noexcept;

	Bootstrap &bootstrap_;
	Summary summary_{};
	uint64_t checked_time_us_{};
	bool pending_{};
	bool started_{};
};

bool sameState(State a, State b) noexcept;
bool validState(State state) noexcept;
const char *kindName(Kind kind) noexcept;
const char *faultName(Fault fault) noexcept;

} // namespace state_capture
} // namespace research_sitl
