#include "NativeClosedOwnerSourceBridge.hpp"
#include "NativeClosedJson.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/time.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int commander_main(int argc, char *argv[]);

namespace research_sitl
{
namespace
{
enum class Phase : uint8_t { Empty, Ready, Claimed, Retired, Abandoned };
enum class BridgeFault : uint8_t {
	None, InvalidBootstrap, BootstrapRepeated, ClaimRejected, SourceThreadInvalid,
	StackUnavailable, StackInsufficient, ClockInvalid, ControlProtocol
};

std::atomic<Phase> phase{Phase::Empty};
std::atomic<BridgeFault> bridge_fault{BridgeFault::None};
std::atomic<bool> initial_claimed{false};
std::atomic<bool> source_thread_bound{false};
std::atomic<bool> source_started{false};
std::atomic<bool> normal_source_done{false};
pthread_t source_thread{};
// Never freed, replaced or reset. Publication occurs only after full bootstrap.
NativeClosedDiagnosticOwner *owner{};
NativeClosedOwnerConfig configuration{};
std::mutex control_mutex; // Source calls and destructor never acquire this lock.
uint32_t bootstrap_count{};
std::atomic<uint32_t> claim_count{0};
uint64_t observation_window_us{};
bool waited{};
bool wait_completed{};
bool stop_attempted{};
int commander_stop_result{-1};
bool stack_measured{};
bool source_turns_enabled{}; // Source-thread-only; no deep owner calls without qualified stack.
uint64_t stack_size_bytes{}, stack_guard_bytes{}, stack_marker_offset_bytes{};
uintptr_t conservative_stack_bottom{}, stack_top{};

// Qualification margins for this Linux x86_64 diagnostic experiment only.
// They do not resize a task stack or establish a hardware stack budget.
constexpr uint64_t MinimumMarkerOffset = 49152;
constexpr uint64_t ObservationWindowUs = 2000000;
constexpr useconds_t WaitPollUs = 1000;

void fault(BridgeFault value) noexcept
{
	auto expected = BridgeFault::None;
	bridge_fault.compare_exchange_strong(expected, value, std::memory_order_release, std::memory_order_relaxed);
}

bool claim() noexcept
{
	auto expected = Phase::Ready;

	if (!phase.compare_exchange_strong(expected, Phase::Claimed, std::memory_order_acq_rel, std::memory_order_acquire)) {
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

	if (pthread_getattr_np(pthread_self(), &attributes) != 0) { fault(BridgeFault::StackUnavailable); return false; }

	void *base{};
	size_t size{}, guard{};
	const int stack_result = pthread_attr_getstack(&attributes, &base, &size);
	const int guard_result = pthread_attr_getguardsize(&attributes, &guard);
	const int destroy_result = pthread_attr_destroy(&attributes);
	const unsigned char marker{};
	const auto bottom = reinterpret_cast<uintptr_t>(base);
	const auto location = reinterpret_cast<uintptr_t>(&marker);

	if (stack_result != 0 || guard_result != 0 || destroy_result != 0 || size == 0 || guard >= size
	    || bottom > std::numeric_limits<uintptr_t>::max() - size || location < bottom + guard || location >= bottom + size) {
		fault(BridgeFault::StackUnavailable);
		return false;
	}

	stack_size_bytes = size;
	stack_guard_bytes = guard;
	// Subtract the reported guard even if the platform excludes it from its
	// reported stack interval; this errs toward less available stack.
	conservative_stack_bottom = bottom + guard;
	stack_top = bottom + size;
	stack_marker_offset_bytes = location - conservative_stack_bottom;
	stack_measured = true;

	if (stack_marker_offset_bytes < MinimumMarkerOffset) { fault(BridgeFault::StackInsufficient); return false; }

	return true;
#endif
}

bool exportReady() noexcept
{
	// Never inspect any non-atomic owner/archive field before these acquires.
	return wait_completed && stop_attempted && commander_stop_result == 0
	       && normal_source_done.load(std::memory_order_acquire)
	       && phase.load(std::memory_order_acquire) == Phase::Retired
	       && bridge_fault.load(std::memory_order_acquire) == BridgeFault::None
	       && bootstrap_count == 1 && claim_count.load(std::memory_order_acquire) == 1;
}

const char *phaseName(Phase value) noexcept
{
	switch (value) {
	case Phase::Empty: return "Empty";

	case Phase::Ready: return "Ready";

	case Phase::Claimed: return "Claimed";

	case Phase::Retired: return "Retired";

	case Phase::Abandoned: return "Abandoned";
	}

	return "Invalid";
}

const char *faultName(BridgeFault value) noexcept
{
	switch (value) {
	case BridgeFault::None: return "None";

	case BridgeFault::InvalidBootstrap: return "InvalidBootstrap";

	case BridgeFault::BootstrapRepeated: return "BootstrapRepeated";

	case BridgeFault::ClaimRejected: return "ClaimRejected";

	case BridgeFault::SourceThreadInvalid: return "SourceThreadInvalid";

	case BridgeFault::StackUnavailable: return "StackUnavailable";

	case BridgeFault::StackInsufficient: return "StackInsufficient";

	case BridgeFault::ClockInvalid: return "ClockInvalid";

	case BridgeFault::ControlProtocol: return "ControlProtocol";
	}

	return "Invalid";
}

bool writeExclusive(const char *path, const std::string &document)
{
	if (!path || !*path) { return false; }

	// Existing paths, symlinks, FIFOs and device sinks are rejected. All filesystem
	// interaction occurs after source completion and retirement, never in stop.
	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK, 0600);

	if (fd < 0) { return false; }

	struct stat info {};

	bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode);

	size_t offset{};

	while (ok && offset < document.size()) {
		const ssize_t count = write(fd, document.data() + offset, document.size() - offset);

		if (count < 0 && errno == EINTR) { continue; }

		if (count <= 0) { ok = false; break; }

		offset += static_cast<size_t>(count);
	}

	if (close(fd) != 0) { ok = false; }

	return ok;
}

int exportLocked(const char *path)
{
	if (!exportReady()) { return 1; }

	using namespace native_closed_json;
	reset();
	const bool completed = normal_source_done.load(std::memory_order_acquire);
	const auto retirement = phase.load(std::memory_order_acquire);
	const auto observed_fault = bridge_fault.load(std::memory_order_acquire);
	const NativeOwnerSummary summary = owner->summary();
	const std::unique_ptr<NativeSourceRecord> record(new (std::nothrow) NativeSourceRecord{});
	const std::unique_ptr<NativeClosureRecord> closure(new (std::nothrow) NativeClosureRecord{});

	if (!record || !closure || !owner->copyClosure(*closure)) { return 1; }

	std::vector<Json> records;
	records.reserve(summary.record_count);
	uint32_t report_count{};

	for (size_t index = 0; index < summary.record_count; ++index) {
		if (!owner->copyRecord(index, *record) || record->result.record_count > 4) { return 1; }

		report_count += record->result.record_count;
		records.push_back(value(*record));
	}

	if (closure->result.record_count > 4) { return 1; }

	report_count += closure->result.record_count;
	const Json document = object({
		{"schema", value("research-c1d-native-owner/v1")},
		{
			"header", object({
				{"profile_id", value(kNativeClosedOwnerProfile)},
				{"execution_origin", value(research_acceleration::kDiagnosticExecutionOrigin)},
				{"clock_source", value("px4_hrt_absolute_time_native_posix")},
				{"configuration", value(configuration)},
				{"normal_record_capacity", value(static_cast<uint32_t>(kNativeSourceRecordCapacity))},
				{"owner_size_bytes", value(static_cast<uint64_t>(sizeof(NativeClosedDiagnosticOwner)))},
				{"observation_window_us", value(observation_window_us)}
			})
		},
		{"records", array(records)}, {"closure", value(*closure)}, {"summary", value(summary)},
		{
			"lifecycle", object({
				{"bootstrap_count", value(bootstrap_count)}, {"claim_count", value(claim_count.load(std::memory_order_acquire))},
				{"normal_source_done", value(completed)}, {"retirement", value(phaseName(retirement))},
				{"commander_stop_result", Json(std::to_string(commander_stop_result))}, {"bridge_fault", value(faultName(observed_fault))},
				{"stack_measured", value(stack_measured)}, {"stack_size_bytes", value(stack_size_bytes)},
				{"stack_guard_bytes", value(stack_guard_bytes)}, {"stack_marker_offset_bytes", value(stack_marker_offset_bytes)},
				{"export_after_retirement", value(completed &&retirement == Phase::Retired)}
			})
		},
		{
			"footer", object({
				{"normal_record_count", value(summary.record_count)}, {"adapter_report_count", value(report_count)},
				{"last_report_sequence", value(closure->result.snapshot.adapter.last_report_sequence)}
			})
		}
	}) + '\n';

	if (!valid() || !exportReady() || !writeExclusive(path, document) || !exportReady()) { return 1; }

	return summary.state == NativeOwnerState::Stopped && summary.fault == NativeOwnerFault::None ? 0 : 1;
}
} // namespace

NativeClosedOwnerSourceBridge::NativeClosedOwnerSourceBridge() noexcept : claimed_(claim()) {}

NativeClosedOwnerSourceBridge::~NativeClosedOwnerSourceBridge()
{
	if (!claimed_) { return; }

	// No owner access, cleanup, I/O or control lock, including forced destruction.
	const auto retired = normal_source_done.load(std::memory_order_acquire) ? Phase::Retired : Phase::Abandoned;
	phase.store(retired, std::memory_order_release);
}

bool NativeClosedOwnerSourceBridge::sourceReady() noexcept
{
	if (!claimed_ || !onSourceThread()) { return false; }

	// Even fault closure has a large result-by-value call chain. If stack
	// qualification failed, retain the archive unexportable without entering it.
	if (!source_turns_enabled) { return false; }

	// Every hook may have a different Commander caller depth. Take a current
	// source-stack marker before ANY owner call, including fault closure.
	// Native .su/disassembly review must bound this marker-to-SP gap and all
	// deeper frames; the marker alone is not an exact stack-pointer bound.
	const unsigned char marker{};
	const auto location = reinterpret_cast<uintptr_t>(&marker);

	if (location < conservative_stack_bottom || location >= stack_top) {
		fault(BridgeFault::StackUnavailable);
		source_turns_enabled = false;
		return false;
	}

	const auto available = location - conservative_stack_bottom;

	if (available < stack_marker_offset_bytes) { stack_marker_offset_bytes = available; }

	if (available < MinimumMarkerOffset) {
		fault(BridgeFault::StackInsufficient);
		source_turns_enabled = false;
		return false;
	}

	if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) {
		owner->sourceContinuityLost();
		return false;
	}

	return true;
}

void NativeClosedOwnerSourceBridge::initial(RawState state) noexcept
{
	if (!claimed_) { return; }

	bool expected = false;

	if (!initial_claimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		fault(BridgeFault::SourceThreadInvalid);

		if (sourceReady()) { owner->sourceContinuityLost(); }

		return;
	}

	source_thread = pthread_self();
	source_thread_bound.store(true, std::memory_order_release);
	source_turns_enabled = measureStack();
	const auto now = hrt_absolute_time();

	if (now == 0) { fault(BridgeFault::ClockInvalid); }

	if (sourceReady()) { owner->initial(state, now); }

	source_started.store(true, std::memory_order_release);
}

void NativeClosedOwnerSourceBridge::transition(RawState before, RawState after) noexcept
{
	if (sourceReady()) { owner->transition(before, after, hrt_absolute_time()); }
}

void NativeClosedOwnerSourceBridge::sample(RawState state) noexcept
{
	if (sourceReady()) { owner->sample(state, hrt_absolute_time()); }
}

void NativeClosedOwnerSourceBridge::finishSource(RawState state) noexcept
{
	if (!claimed_ || !onSourceThread()) { return; }

	if (sourceReady()) { owner->stop(state, hrt_absolute_time()); }

	// The archive can be faulted already; normal run completion is a separate fact.
	normal_source_done.store(true, std::memory_order_release);
}

int NativeClosedOwnerSourceBridge::bootstrap(const NativeClosedOwnerConfig &config) noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (phase.load(std::memory_order_acquire) != Phase::Empty || owner != nullptr) {
		fault(BridgeFault::BootstrapRepeated);
		return 1;
	}

	if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) { return 1; }

	owner = new (std::nothrow) NativeClosedDiagnosticOwner(config);

	if (!owner || owner->summary().state != NativeOwnerState::AwaitingInitial) {
		fault(BridgeFault::InvalidBootstrap);
		return 1;
	}

	configuration = config;
	bootstrap_count = 1;
	phase.store(Phase::Ready, std::memory_order_release);
	return 0;
}

int NativeClosedOwnerSourceBridge::rejectBootstrap() noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);
	fault(BridgeFault::InvalidBootstrap);
	return 1;
}

int NativeClosedOwnerSourceBridge::waitWindow(uint64_t window_us) noexcept
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (window_us != ObservationWindowUs || waited || stop_attempted) { fault(BridgeFault::ControlProtocol); return 1; }

	const auto start = hrt_absolute_time();
	auto previous = start;

	if (start == 0) { fault(BridgeFault::ClockInvalid); return 1; }

	// The explicit window also bounds waiting for initial source startup; it is
	// diagnostic orchestration, not a state freshness or command timing budget.
	while (!source_started.load(std::memory_order_acquire)) {
		const auto now = hrt_absolute_time();

		if (now < previous) { fault(BridgeFault::ClockInvalid); return 1; }

		previous = now;

		if (now - start >= window_us || bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) { return 1; }

		px4_usleep(WaitPollUs);
	}

	const auto observation_start = hrt_absolute_time();

	if (observation_start < previous) { fault(BridgeFault::ClockInvalid); return 1; }

	previous = observation_start;
	observation_window_us = window_us;
	waited = true;

	for (;;) {
		const auto now = hrt_absolute_time();

		if (now < previous) { fault(BridgeFault::ClockInvalid); return 1; }

		previous = now;

		if (bridge_fault.load(std::memory_order_acquire) != BridgeFault::None) { return 1; }

		if (now - observation_start >= window_us) { wait_completed = true; return 0; }

		px4_usleep(WaitPollUs);
	}
}

int NativeClosedOwnerSourceBridge::stopExport(const char *path)
{
	const std::lock_guard<std::mutex> lock(control_mutex);

	if (stop_attempted) { fault(BridgeFault::ControlProtocol); return 1; }

	stop_attempted = true;
	char commander[] = "commander";
	char stop[] = "stop";
	char *arguments[] {commander, stop, nullptr};
	commander_stop_result = commander_main(2, arguments);
	return exportLocked(path);
}

int NativeClosedOwnerSourceBridge::exportClosed(const char *path)
{
	const std::lock_guard<std::mutex> lock(control_mutex);
	return exportLocked(path);
}

} // namespace research_sitl
