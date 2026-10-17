#pragma once

#if !defined(RESEARCH_CLOSED_MERGE_HOST) || RESEARCH_CLOSED_MERGE_HOST != 1
#error "Closed receipt merge requires its standalone host target"
#endif
#if !defined(__linux__) || defined(__PX4_POSIX) || defined(__PX4_NUTTX) || defined(__PX4_LINUX) \
	|| defined(__PX4_ROS2) || defined(__NuttX__) || defined(CONFIG_ARCH_BOARD) || defined(CONFIG_ARCH_BOARD_PX4_SITL) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(BOARD_LOCKSTEP) || defined(PX4_SIMULATOR_TIMESTAMP_PROXY) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "Closed receipt merge is not released for native PX4 or hardware"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace research_sitl::provenance
{
constexpr size_t SourceCapacity = 4;
constexpr uint32_t QueueCapacity = 16;
constexpr size_t ArchiveCapacity = 4096;
constexpr size_t ObservationWords = 7;

struct Identity {
	uint64_t run{}, owner{}, controller{}, publisher{}, lifetime{}, clock{};
};

enum class Kind : uint8_t { Initial, Sample, Lost };
struct Receipt {
	Identity identity{};
	uint64_t sequence{}, source_time_us{};
	Kind kind{Kind::Initial};
	std::array<uint64_t, ObservationWords> observation{};
};

struct Config {
	size_t source_count{};
	std::array<Identity, SourceCapacity> sources{};
};

enum class Fault : uint8_t {
	None, InvalidConfiguration, InvalidSlot, ConcurrentProducer, QueueOverflow,
	CounterExhausted, IdentityMismatch, MissingInitial, RepeatedInitial,
	Sequence, SourceTime, InvalidKind, SourceLost, EvidenceExhausted,
	OwnerTime, OwnerStop
};

struct Record {
	uint64_t owner_order{}, owner_time_us{};
	size_t slot{};
	Receipt receipt{};
};

struct Closure {
	Fault cause{Fault::None};
	size_t slot{};
	uint64_t owner_time_us{};
	bool has_attempted{};
	size_t attempted_slot{};
	Receipt attempted{};
};

// Closed observation mechanics only. No authority, state generation or map API.
// One owner; producer lifetime must end before destruction/export of queues.
class ReceiptMerge final
{
public:
	explicit ReceiptMerge(const Config &config) noexcept;
	ReceiptMerge(const ReceiptMerge &) = delete;
	ReceiptMerge &operator=(const ReceiptMerge &) = delete;
	bool publish(size_t slot, const Receipt &receipt) noexcept;
	bool step(uint64_t owner_time_us) noexcept; // at most one committed record
	void stop(uint64_t owner_time_us) noexcept;
	// Owner-thread-only or after owner and all producers have joined.
	size_t count() const noexcept { return count_; }
	bool closed() const noexcept { return closed_; }
	Closure closure() const noexcept { return closure_; }
	bool copy(size_t index, Record &out) const noexcept;

private:
	friend struct MergeTestPeer;
	struct Lane {
		std::array<Receipt, QueueCapacity> queue{};
		std::atomic<uint32_t> read{0}, write{0};
		std::atomic_flag writer = ATOMIC_FLAG_INIT;
		uint64_t last_sequence{}, last_source_time{}; // owner only
	};
	static_assert(std::atomic<uint32_t>::is_always_lock_free);
	static_assert(std::atomic<bool>::is_always_lock_free);
	void fault(Fault cause, size_t slot) noexcept;
	void close(uint64_t time, const Receipt *attempted = nullptr, size_t attempted_slot = 0) noexcept;
	Fault validate(size_t slot, const Receipt &receipt) const noexcept;
	const Config config_;
	std::array<Lane, SourceCapacity> lanes_{};
	std::atomic<uint32_t> first_fault_{0};
	std::atomic<bool> stopped_{false};
	std::array<Record, ArchiveCapacity> archive_{};
	size_t count_{}, cursor_{};
	uint64_t last_owner_time_{};
	bool closed_{};
	Closure closure_{}; // terminal capacity reserved independently
};
}
