#include "NativeIngressSourceBridge.hpp"
#include "NativeIngressWorker.hpp"
#include "NativeIngressJson.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/time.h>

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <time.h>

extern "C" int commander_main(int argc, char *argv[]);

namespace research_sitl
{
namespace
{
enum class BridgePhase : uint8_t { Empty, Ready, Claimed, Retired, Abandoned };
enum class BridgeFault : uint8_t {
	None, InvalidBootstrap, BootstrapRepeated, ClaimRejected, SourceThreadInvalid,
	StackUnavailable, StackInsufficient, ClockInvalid, WorkerStartFailed,
	WorkerJoinFailed, ControlProtocol
};

std::atomic<BridgePhase> phase{BridgePhase::Empty};
std::atomic<BridgeFault> bridge_fault{BridgeFault::None};
std::atomic<bool> initial_claimed{false};
std::atomic<bool> source_thread_bound{false};
std::atomic<bool> source_started{false};
std::atomic<bool> normal_source_done{false};
std::atomic<bool> worker_joined{false};
std::atomic<uint64_t> checkpoint_version{0};
std::atomic<uint64_t> checkpoint_packed{0};
std::atomic<uint64_t> checkpoint_sequence{0};
std::atomic<uint64_t> checkpoint_checked_hrt_us{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free,
	      "C1E source checkpoint requires lock-free 64-bit atomics");
pthread_t source_thread{};
pthread_t worker_thread{};
// These objects are installed once, never replaced or freed after publication.
NativeIngressMailbox *mailbox{};
NativeIngressDiagnosticOwner *owner{};
NativeIngressWorker *worker{};
NativeIngressOwnerConfig configuration{};
std::mutex control_mutex; // Source hooks never acquire this mutex.
uint32_t bootstrap_count{};
std::atomic<uint32_t> claim_count{0};
bool waited{};
bool wait_completed{};
uint64_t observation_window_us{};
bool stop_attempted{};
int commander_stop_result{-1};
bool stack_measured{};
bool source_turns_enabled{};
uint64_t stack_size_bytes{}, stack_guard_bytes{}, stack_marker_offset_bytes{};
uintptr_t conservative_stack_bottom{}, stack_top{};

constexpr uint64_t kMinimumMarkerOffset = 49152;
constexpr uint64_t kObservationWindowUs = 2000000;
constexpr useconds_t kWorkerIdleSleepUs = 1000;
constexpr useconds_t kWaitPollUs = 1000;
constexpr time_t kJoinTimeoutSeconds = 2;
constexpr uint64_t kCheckpointEpochMax = (uint64_t{1} << 48) - 1;
constexpr uint64_t kCheckpointWaitUs = 2000000;

void clearKey(uint8_t (&key)[32]) noexcept
{
	volatile uint8_t *bytes = key;

	for (size_t index = 0; index < sizeof(key); ++index) { bytes[index] = 0; }
}

void fault(BridgeFault value) noexcept
{
	auto expected = BridgeFault::None;
	bridge_fault.compare_exchange_strong(expected, value, std::memory_order_release,
					     std::memory_order_relaxed);
}

bool claim() noexcept
{
	auto expected = BridgePhase::Ready;

	if (!phase.compare_exchange_strong(expected, BridgePhase::Claimed,
					   std::memory_order_acq_rel, std::memory_order_acquire)) {
		fault(BridgeFault::ClaimRejected);
		return false;
	}

	claim_count.store(1, std::memory_order_release);
	return true;
}

bool onSourceThread() noexcept
{
	if (!source_thread_bound.load(std::memory_order_acquire) || !pthread_equal(source_thread, pthread_self())) {
		fault(BridgeFault::SourceThreadInvalid);

		if (mailbox) { mailbox->terminal.store(true, std::memory_order_release); }

		return false;
	}

	return true;
}

bool measureStack() noexcept
{
#if !defined(__x86_64__)
	fault(BridgeFault::StackUnavailable);
	return false;
#else
	pthread_attr_t attributes;

	if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
		fault(BridgeFault::StackUnavailable);
		return false;
	}

	void *base{};
	size_t size{}, guard{};
	const int stack_result = pthread_attr_getstack(&attributes, &base, &size);
	const int guard_result = pthread_attr_getguardsize(&attributes, &guard);
	const int destroy_result = pthread_attr_destroy(&attributes);
	const unsigned char marker{};
	const auto bottom = reinterpret_cast<uintptr_t>(base);
	const auto location = reinterpret_cast<uintptr_t>(&marker);

	if (stack_result != 0 || guard_result != 0 || destroy_result != 0 || size == 0 || guard >= size
	    || bottom > std::numeric_limits<uintptr_t>::max() - size
	    || location < bottom + guard || location >= bottom + size) {
		fault(BridgeFault::StackUnavailable);
		return false;
	}

	stack_size_bytes = size;
	stack_guard_bytes = guard;
	conservative_stack_bottom = bottom + guard;
	stack_top = bottom + size;
	stack_marker_offset_bytes = location - conservative_stack_bottom;
	stack_measured = true;

	if (stack_marker_offset_bytes < kMinimumMarkerOffset) {
		fault(BridgeFault::StackInsufficient);
		return false;
	}

	return true;
#endif
}

void publishCheckpoint(RawState state) noexcept
{
	// Only the Commander source thread reads owner state. Control readers acquire
	// a consistent atomic projection and never touch the live owner.
	const NativeIngressOwnerSummary observed = owner->summary();

	if (observed.state != NativeOwnerState::Observing || !observed.source.initialized
	    || observed.source.state.arming_state != state.arming_state
	    || observed.source.state.nav_state != state.nav_state) { return; }

	const auto &source = observed.source;
	const uint64_t version = checkpoint_version.load(std::memory_order_seq_cst);

	if (source.transition_epoch == 0 || source.transition_epoch > kCheckpointEpochMax
	    || source.sequence == 0 || source.checked_time_us == 0
	    || version > std::numeric_limits<uint64_t>::max() - 2) {
		fault(BridgeFault::ControlProtocol);
		mailbox->terminal.store(true, std::memory_order_release);
		return;
	}

	const uint64_t packed = (source.transition_epoch << 16)
				| (uint64_t(state.arming_state) << 8) | state.nav_state;
	checkpoint_version.store(version + 1, std::memory_order_seq_cst);
	checkpoint_sequence.store(source.sequence, std::memory_order_seq_cst);
	checkpoint_checked_hrt_us.store(source.checked_time_us, std::memory_order_seq_cst);
	checkpoint_packed.store(packed, std::memory_order_seq_cst);
	checkpoint_version.store(version + 2, std::memory_order_seq_cst);
}

void *workerMain(void *) noexcept
{
	for (;;) {
		const bool progress = worker->pumpOnce();
		const auto slot = mailbox->phase.load(std::memory_order_acquire);

		if (worker->stopRequested() && slot == NativeIngressPhase::Empty) { break; }

		if (!progress) { px4_usleep(kWorkerIdleSleepUs); }
	}

	worker->retire();
	return nullptr;
}

bool bindIdentities(const NativeIngressOwnerConfig &owner_config,
		    const DiagnosticParserConfig &parser_config, uint16_t local_port) noexcept
{
	return local_port != 0 && owner_config.peer_port != 0 && local_port != owner_config.peer_port
	       && parser_config.native_run_id == owner_config.identity.native_run_id
	       && parser_config.owner_lifetime_id == owner_config.identity.owner_lifetime_id
	       && parser_config.controller_instance_id == owner_config.adapter_identity.controller_instance_id
	       && parser_config.source_id == owner_config.contract.permitted_source
	       && parser_config.transport_session_id == owner_config.transport_binding_id
	       && parser_config.channel == owner_config.channel;
}

bool joinWorker() noexcept
{
	if (!worker) { return false; }

	worker->requestStop();
	timespec deadline{};

	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0
	    || deadline.tv_sec > std::numeric_limits<time_t>::max() - kJoinTimeoutSeconds) {
		fault(BridgeFault::WorkerJoinFailed);
		return false;
	}

	deadline.tv_sec += kJoinTimeoutSeconds;

	if (pthread_timedjoin_np(worker_thread, nullptr, &deadline) != 0) {
		fault(BridgeFault::WorkerJoinFailed);
		return false;
	}

	worker_joined.store(true, std::memory_order_release);
	return true;
}

bool exportReady() noexcept
{
	return owner && worker && mailbox && stop_attempted && commander_stop_result == 0
	       && normal_source_done.load(std::memory_order_acquire)
	       && worker_joined.load(std::memory_order_acquire)
	       && phase.load(std::memory_order_acquire) == BridgePhase::Retired
	       && bootstrap_count == 1 && claim_count.load(std::memory_order_acquire) == 1;
}

int exportLocked(const char *path)
{
	if (!exportReady()) { return 1; }

	native_ingress_json::Lifecycle lifecycle{};
	lifecycle.bootstrap_count = bootstrap_count;
	lifecycle.claim_count = claim_count.load(std::memory_order_acquire);
	lifecycle.stack_size_bytes = stack_size_bytes;
	lifecycle.stack_guard_bytes = stack_guard_bytes;
	lifecycle.stack_marker_offset_bytes = stack_marker_offset_bytes;
	lifecycle.observation_window_us = observation_window_us;
	lifecycle.commander_stop_result = commander_stop_result;
	lifecycle.bridge_fault = static_cast<uint8_t>(bridge_fault.load(std::memory_order_acquire));
	lifecycle.source_done = normal_source_done.load(std::memory_order_acquire);
	lifecycle.worker_joined = worker_joined.load(std::memory_order_acquire);
	lifecycle.stack_measured = stack_measured;
	lifecycle.waited = waited;
	lifecycle.wait_completed = wait_completed;
	return native_ingress_json::writeArchive(path, *owner, *worker, configuration, lifecycle) ? 0 : 1;
}
} // namespace

NativeIngressSourceBridge::NativeIngressSourceBridge() noexcept : claimed_(claim()) {}

NativeIngressSourceBridge::~NativeIngressSourceBridge()
{
	if (!claimed_) { return; }

	const auto retired = normal_source_done.load(std::memory_order_acquire)
			     ? BridgePhase::Retired : BridgePhase::Abandoned;
	phase.store(retired, std::memory_order_release);
}

bool NativeIngressSourceBridge::sourceReady(RawState current) noexcept
{
	if (!claimed_ || !onSourceThread()) { return false; }

	if (!source_turns_enabled) {
		mailbox->terminal.store(true, std::memory_order_release);
		return false;
	}

	// Marker-to-SP depth and deeper owner frames require separate native stack review.
	const unsigned char marker{};
	const auto location = reinterpret_cast<uintptr_t>(&marker);

	if (location < conservative_stack_bottom || location >= stack_top) {
		fault(BridgeFault::StackUnavailable);
		source_turns_enabled = false;
		mailbox->terminal.store(true, std::memory_order_release);
		return false;
	}

	const auto available = location - conservative_stack_bottom;

	if (available < stack_marker_offset_bytes) { stack_marker_offset_bytes = available; }

	if (available < kMinimumMarkerOffset) {
		fault(BridgeFault::StackInsufficient);
		source_turns_enabled = false;
		mailbox->terminal.store(true, std::memory_order_release);
		return false;
	}

	if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) {
		owner->stopOrFault(NativeIngressCloseCause::SourceContinuityLost, current, hrt_absolute_time());
		return false;
	}

	return true;
}

void NativeIngressSourceBridge::initial(RawState state) noexcept
{
	if (!claimed_) { return; }

	bool expected = false;

	if (initial_claimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		source_thread = pthread_self();
		source_thread_bound.store(true, std::memory_order_release);
		source_turns_enabled = measureStack();
	}

	if (sourceReady(state)) {
		owner->initial(state, hrt_absolute_time());
		publishCheckpoint(state);
	}

	source_started.store(true, std::memory_order_release);
}

void NativeIngressSourceBridge::transition(RawState before, RawState after) noexcept
{
	if (sourceReady(after)) {
		owner->transition(before, after, hrt_absolute_time());
		publishCheckpoint(after);
	}
}

void NativeIngressSourceBridge::sample(RawState state) noexcept
{
	if (sourceReady(state)) {
		owner->sample(state, hrt_absolute_time());
		publishCheckpoint(state);
	}
}

void NativeIngressSourceBridge::boundary(RawState current) noexcept
{
	if (sourceReady(current)) { owner->resolveReady(current, hrt_absolute_time()); }
}

void NativeIngressSourceBridge::finishSource(RawState state) noexcept
{
	if (!claimed_ || !onSourceThread()) { return; }

	if (sourceReady(state)) {
		owner->stopOrFault(NativeIngressCloseCause::OwnerStop, state, hrt_absolute_time());
	}

	normal_source_done.store(true, std::memory_order_release);
}

int NativeIngressSourceBridge::bootstrap(const NativeIngressOwnerConfig &owner_config,
		DiagnosticParserConfig &parser_config, uint16_t local_port) noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (phase.load(std::memory_order_acquire) != BridgePhase::Empty || owner || worker || mailbox) {
		clearKey(parser_config.key);
		fault(BridgeFault::BootstrapRepeated);
		return 1;
	}

	if (!bindIdentities(owner_config, parser_config, local_port)
	    || bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) {
		clearKey(parser_config.key);
		fault(BridgeFault::InvalidBootstrap);
		return 1;
	}

	std::unique_ptr<NativeIngressMailbox> pending_mailbox(new (std::nothrow) NativeIngressMailbox{});

	if (!pending_mailbox) { clearKey(parser_config.key); fault(BridgeFault::InvalidBootstrap); return 1; }

	std::unique_ptr<NativeIngressDiagnosticOwner> pending_owner(
		new (std::nothrow) NativeIngressDiagnosticOwner(owner_config, *pending_mailbox));
	std::unique_ptr<NativeIngressWorker> pending_worker(
		new (std::nothrow) NativeIngressWorker(parser_config, *pending_mailbox,
				local_port, owner_config.peer_port));
	clearKey(parser_config.key);

	if (!pending_owner || !pending_worker || pending_owner->summary().state != NativeOwnerState::AwaitingInitial
	    || !pending_worker->start()) {
		fault(BridgeFault::InvalidBootstrap);
		return 1;
	}

	mailbox = pending_mailbox.release();
	owner = pending_owner.release();
	worker = pending_worker.release();
	configuration = owner_config;

	if (pthread_create(&worker_thread, nullptr, workerMain, nullptr) != 0) {
		worker->retire();
		fault(BridgeFault::WorkerStartFailed);
		return 1;
	}

	bootstrap_count = 1;
	phase.store(BridgePhase::Ready, std::memory_order_release);
	return 0;
}

int NativeIngressSourceBridge::rejectBootstrap() noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);
	fault(BridgeFault::InvalidBootstrap);
	return 1;
}

int NativeIngressSourceBridge::printCheckpoint() noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (bootstrap_count != 1 || !owner || !mailbox || !worker) { return 1; }

	const uint64_t start = hrt_absolute_time();
	uint64_t previous = start;

	if (start == 0) { return 1; }

	for (;;) {
		if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None
		    || mailbox->terminal.load(std::memory_order_acquire)
		    || normal_source_done.load(std::memory_order_acquire)) { return 1; }

		const uint64_t before = checkpoint_version.load(std::memory_order_seq_cst);

		if (before != 0 && (before & 1U) == 0) {
			const uint64_t packed = checkpoint_packed.load(std::memory_order_seq_cst);
			const uint64_t sequence = checkpoint_sequence.load(std::memory_order_seq_cst);
			const uint64_t checked = checkpoint_checked_hrt_us.load(std::memory_order_seq_cst);
			const uint64_t after = checkpoint_version.load(std::memory_order_seq_cst);
			const uint64_t epoch = packed >> 16;
			const uint8_t arm = static_cast<uint8_t>((packed >> 8) & 0xffU);
			const uint8_t nav = static_cast<uint8_t>(packed & 0xffU);

			if (before == after && (after & 1U) == 0 && epoch != 0 && sequence >= 3 && checked != 0
			    && arm == 1 && nav < 31 && nav != 9 && nav != 11 && nav != 16
			    && !mailbox->terminal.load(std::memory_order_acquire)) {
				if (std::printf("C1E_SOURCE_CHECKPOINT epoch=%" PRIu64 " arm=%u nav=%u sequence=%" PRIu64
						" checked_hrt_us=%" PRIu64 "\n",
						epoch, static_cast<unsigned>(arm), static_cast<unsigned>(nav), sequence, checked) < 0
				    || std::fflush(stdout) != 0) { return 1; }

				return 0;
			}
		}

		const uint64_t now = hrt_absolute_time();

		if (now < previous || now - start >= kCheckpointWaitUs) { return 1; }

		previous = now;
		px4_usleep(kWaitPollUs);
	}
}

int NativeIngressSourceBridge::waitWindow(uint64_t window_us) noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (window_us != kObservationWindowUs || waited || stop_attempted || bootstrap_count != 1) {
		fault(BridgeFault::ControlProtocol);
		return 1;
	}

	const auto start = hrt_absolute_time();
	auto previous = start;

	if (start == 0) { fault(BridgeFault::ClockInvalid); return 1; }

	while (!source_started.load(std::memory_order_acquire)) {
		const auto now = hrt_absolute_time();

		if (now < previous) { fault(BridgeFault::ClockInvalid); return 1; }

		previous = now;

		if (now - start >= window_us || bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) {
			return 1;
		}

		px4_usleep(kWaitPollUs);
	}

	const auto observation_start = hrt_absolute_time();

	if (observation_start < previous) { fault(BridgeFault::ClockInvalid); return 1; }

	previous = observation_start;
	waited = true;
	observation_window_us = window_us;

	for (;;) {
		const auto now = hrt_absolute_time();

		if (now < previous) { fault(BridgeFault::ClockInvalid); return 1; }

		previous = now;

		if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) { return 1; }

		if (now - observation_start >= window_us) { wait_completed = true; return 0; }

		px4_usleep(kWaitPollUs);
	}
}

int NativeIngressSourceBridge::stopExport(const char *path)
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (stop_attempted || !worker || bootstrap_count != 1) {
		fault(BridgeFault::ControlProtocol);
		return 1;
	}

	stop_attempted = true;
	char commander[] = "commander";
	char stop[] = "stop";
	char *arguments[] {commander, stop, nullptr};
	commander_stop_result = commander_main(2, arguments);

	if (!joinWorker()) { return 1; }

	return exportLocked(path);
}

int NativeIngressSourceBridge::exportClosed(const char *path)
{
	const std::lock_guard<std::mutex> lock(control_mutex);
	return exportLocked(path);
}

} // namespace research_sitl
