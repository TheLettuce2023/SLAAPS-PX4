// Capture collector only: no admission owner, command publisher or output path.
#include "../../../producer/Px4StateReceiptProducer.hpp"

#include <px4_platform_common/log.h>
#include <px4_platform_common/time.h>
#include <drivers/drv_hrt.h>
#include <uORB/uORB.h>

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

using namespace research_sitl::state_capture;

namespace
{
constexpr size_t ReceiptCapacity = 4096; // Bounded evidence storage, not a freshness budget.
constexpr size_t SummaryCapacity = 8;
constexpr unsigned ReceiptQueueDepth = 32;
constexpr unsigned DrainBatch = ReceiptQueueDepth * 2;
constexpr useconds_t PollSleepUs = 1000; // Collector scheduling only, no acceptance deadline.

enum class CollectorFault {
	None, InvalidArguments, BootstrapFailed, SubscriptionFailed, StreamAlreadyPublished,
	ReceiptCopyFailed, SummaryCopyFailed, BufferOverflow, TimeInvalid, TimeRegressed,
	DuplicateSummary, MissingSummary, ProducerFailed, FinalMismatch, EvidenceWriteFailed, InvalidPhase
};
enum class Phase { Empty, Bootstrapped, Collected, Finished };

struct ReceivedReceipt {
	research_state_receipt_s value{};
	uint64_t receive_time_us{};
};
struct ReceivedSummary {
	research_state_summary_s value{};
	uint64_t receive_time_us{};
};

std::mutex collector_mutex;
Phase phase{Phase::Empty};
CollectorFault first_fault{CollectorFault::None};
Identities identities{};
int receipt_fd{-1};
int summary_fd{-1};
std::array<ReceivedReceipt, ReceiptCapacity> receipts{};
std::array<ReceivedSummary, SummaryCapacity> summaries{};
size_t receipt_count{};
size_t summary_count{};
uint64_t last_receive_us{};
uint64_t window_us{};
bool final_drain_complete{};

void fail(CollectorFault fault)
{
	if (first_fault == CollectorFault::None) { first_fault = fault; }
}

const char *collectorFaultName(CollectorFault value)
{
	switch (value) {
	case CollectorFault::None: return "None";

	case CollectorFault::InvalidArguments: return "InvalidArguments";

	case CollectorFault::BootstrapFailed: return "BootstrapFailed";

	case CollectorFault::SubscriptionFailed: return "SubscriptionFailed";

	case CollectorFault::StreamAlreadyPublished: return "StreamAlreadyPublished";

	case CollectorFault::ReceiptCopyFailed: return "ReceiptCopyFailed";

	case CollectorFault::SummaryCopyFailed: return "SummaryCopyFailed";

	case CollectorFault::BufferOverflow: return "BufferOverflow";

	case CollectorFault::TimeInvalid: return "TimeInvalid";

	case CollectorFault::TimeRegressed: return "TimeRegressed";

	case CollectorFault::DuplicateSummary: return "DuplicateSummary";

	case CollectorFault::MissingSummary: return "MissingSummary";

	case CollectorFault::ProducerFailed: return "ProducerFailed";

	case CollectorFault::FinalMismatch: return "FinalMismatch";

	case CollectorFault::EvidenceWriteFailed: return "EvidenceWriteFailed";

	case CollectorFault::InvalidPhase: return "InvalidPhase";
	}

	return nullptr;
}

bool parsePositive(const char *text, uint64_t &value)
{
	if (!text || *text == '\0' || *text == '0') { return false; }

	uint64_t parsed{};

	for (const char *cursor = text; *cursor; ++cursor) {
		if (*cursor < '0' || *cursor > '9') { return false; }

		const uint64_t digit = static_cast<uint64_t>(*cursor - '0');

		if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) { return false; }

		parsed = parsed * 10 + digit;
	}

	value = parsed;
	return true;
}

bool receiveTime(uint64_t &now)
{
	now = hrt_absolute_time();

	if (now == 0) { fail(CollectorFault::TimeInvalid); return false; }

	if (now < last_receive_us) { fail(CollectorFault::TimeRegressed); return false; }

	last_receive_us = now;
	return true;
}

bool available(int fd, bool &updated, CollectorFault fault)
{
	updated = false;

	if (orb_check(fd, &updated) != PX4_OK) { fail(fault); return false; }

	return true;
}

bool drain()
{
	for (unsigned i = 0; i < DrainBatch; ++i) {
		bool updated{};

		if (!available(receipt_fd, updated, CollectorFault::ReceiptCopyFailed)) { return false; }

		if (!updated) { break; }

		ReceivedReceipt received{};

		if (orb_copy(ORB_ID(research_state_receipt), receipt_fd, &received.value) != PX4_OK) {
			fail(CollectorFault::ReceiptCopyFailed);
			return false;
		}

		if (!receiveTime(received.receive_time_us)) { return false; }

		if (receipt_count == receipts.size()) { fail(CollectorFault::BufferOverflow); return false; }

		receipts[receipt_count++] = received;
	}

	for (unsigned i = 0; i < DrainBatch; ++i) {
		bool updated{};

		if (!available(summary_fd, updated, CollectorFault::SummaryCopyFailed)) { return false; }

		if (!updated) { break; }

		ReceivedSummary received{};

		if (orb_copy(ORB_ID(research_state_summary), summary_fd, &received.value) != PX4_OK) {
			fail(CollectorFault::SummaryCopyFailed);
			return false;
		}

		if (!receiveTime(received.receive_time_us)) { return false; }

		if (summary_count == summaries.size()) { fail(CollectorFault::BufferOverflow); return false; }

		summaries[summary_count++] = received;

		if (summary_count > 1) { fail(CollectorFault::DuplicateSummary); }
	}

	return true;
}

int bootstrap(int argc, char *argv[])
{
	if (phase != Phase::Empty) {
		// Also invalidate the reserved producer lifetime, even if a caller tries
		// to repeat bootstrap while Commander is still running.
		captureBootstrap().configure({});
		fail(CollectorFault::BootstrapFailed);
		return 1;
	}

	if (argc != 5 || !parsePositive(argv[2], identities.producer_lifetime_id)
	    || !parsePositive(argv[3], identities.observer_lifetime_id)
	    || !parsePositive(argv[4], identities.clock_epoch)) {
		fail(CollectorFault::InvalidArguments);
		return 1;
	}

	if (orb_exists(ORB_ID(research_state_receipt), 0) == PX4_OK
	    || orb_exists(ORB_ID(research_state_summary), 0) == PX4_OK) {
		fail(CollectorFault::StreamAlreadyPublished);
		return 1;
	}

	// These synchronous C subscriptions create unadvertised nodes. Keep the
	// exact descriptors throughout the capture; do not lazily attach after Initial.
	receipt_fd = orb_subscribe(ORB_ID(research_state_receipt));
	summary_fd = orb_subscribe(ORB_ID(research_state_summary));

	if (receipt_fd < 0 || summary_fd < 0) {
		fail(CollectorFault::SubscriptionFailed);
		return 1;
	}

	bool receipt_updated{};
	bool summary_updated{};

	if (!available(receipt_fd, receipt_updated, CollectorFault::ReceiptCopyFailed)
	    || !available(summary_fd, summary_updated, CollectorFault::SummaryCopyFailed)) { return 1; }

	if (receipt_updated || summary_updated) { fail(CollectorFault::StreamAlreadyPublished); return 1; }

	if (!captureBootstrap().configure(identities)) { fail(CollectorFault::BootstrapFailed); return 1; }

	phase = Phase::Bootstrapped;
	return 0;
}

int collect(int argc, char *argv[])
{
	if (phase != Phase::Bootstrapped) { fail(CollectorFault::InvalidPhase); return 1; }

	phase = Phase::Collected; // Failure still permits post-stop export of incomplete evidence.

	if (argc != 3 || !parsePositive(argv[2], window_us)) { fail(CollectorFault::InvalidArguments); return 1; }

	uint64_t start{};

	if (!receiveTime(start)) { return 1; }

	for (;;) {
		if (!drain()) { return 1; }

		uint64_t now{};

		if (!receiveTime(now)) { return 1; }

		if (now - start >= window_us) { break; }

		px4_usleep(PollSleepUs);
	}

	return first_fault == CollectorFault::None ? 0 : 1;
}

const char *jsonBool(bool value) { return value ? "true" : "false"; }

bool writeState(FILE *file, unsigned arm, unsigned nav)
{
	return fprintf(file, "{\"arming_state\":%u,\"nav_state\":%u}", arm, nav) >= 0;
}

bool writeReceipt(FILE *file, const ReceivedReceipt &received)
{
	const auto &r = received.value;
	const char *kind = kindName(static_cast<Kind>(r.kind));

	if (!kind || r.timestamp != r.producer_time_us) { return false; }

	return fprintf(file, "{\"type\":\"receipt\",\"observer_lifetime_id\":\"%" PRIu64
		       "\",\"receive_time_us\":\"%" PRIu64 "\",\"receipt\":{"
		       "\"producer_lifetime_id\":\"%" PRIu64 "\",\"clock_epoch\":\"%" PRIu64
		       "\",\"sequence\":\"%" PRIu64 "\",\"transition_epoch\":\"%" PRIu64
		       "\",\"producer_time_us\":\"%" PRIu64 "\",\"kind\":\"%s\",\"before\":",
		       identities.observer_lifetime_id, received.receive_time_us, r.producer_lifetime_id,
		       r.clock_epoch, r.sequence, r.transition_epoch, r.producer_time_us, kind) >= 0
	       && writeState(file, r.before_arming_state, r.before_nav_state)
	       && fprintf(file, ",\"after\":") >= 0
	       && writeState(file, r.after_arming_state, r.after_nav_state)
	       && fprintf(file, "}}\n") >= 0;
}

bool writeSummary(FILE *file, const ReceivedSummary &received)
{
	const auto &s = received.value;
	const char *fault = faultName(static_cast<Fault>(s.first_fault));

	if (!fault || s.timestamp != s.final_time_us) { return false; }

	return fprintf(file, "{\"type\":\"summary\",\"receive_time_us\":\"%" PRIu64
		       "\",\"summary\":{\"identities\":{\"producer_lifetime_id\":\"%" PRIu64
		       "\",\"observer_lifetime_id\":\"%" PRIu64 "\",\"clock_epoch\":\"%" PRIu64
		       "\"},\"attempted_sequence\":\"%" PRIu64 "\",\"emitted_sequence\":\"%" PRIu64
		       "\",\"transition_epoch\":\"%" PRIu64 "\",\"producer_time_us\":\"%" PRIu64
		       "\",\"final_time_us\":\"%" PRIu64 "\",\"retained_state\":",
		       received.receive_time_us, s.producer_lifetime_id, s.observer_lifetime_id, s.clock_epoch,
		       s.attempted_sequence, s.emitted_sequence, s.transition_epoch, s.producer_time_us, s.final_time_us) >= 0
	       && writeState(file, s.retained_arming_state, s.retained_nav_state)
	       && fprintf(file, ",\"emitted_state\":") >= 0
	       && writeState(file, s.emitted_arming_state, s.emitted_nav_state)
	       && fprintf(file, ",\"emitted_transition_epoch\":\"%" PRIu64
			  "\",\"emitted_producer_time_us\":\"%" PRIu64 "\",\"actual_state\":",
			  s.emitted_transition_epoch, s.emitted_producer_time_us) >= 0
	       && writeState(file, s.actual_arming_state, s.actual_nav_state)
	       && fprintf(file, ",\"has_state\":%s,\"finished\":%s,\"first_fault\":\"%s\"}}\n",
			  jsonBool(s.has_state), jsonBool(s.finished), fault) >= 0;
}

bool writeEvidence()
{
	// Exclusive regular file only. Existing paths (including pipes/symlinks)
	// are rejected. Even an unavailable/slow filesystem is reached only after stop.
	const int fd = open("capture.jsonl", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) { return false; }

	struct stat info {};

	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) { close(fd); return false; }

	FILE *file = fdopen(fd, "w");

	if (!file) { close(fd); return false; }

	bool ok = fprintf(file, "{\"type\":\"bootstrap\",\"schema\":\"research-c1c-capture/v1\","
			  "\"clock_source\":\"px4_hrt_absolute_time_native_posix\","
			  "\"producer_lifetime_id\":\"%" PRIu64 "\",\"observer_lifetime_id\":\"%" PRIu64
			  "\",\"clock_epoch\":\"%" PRIu64 "\",\"receipt_queue_depth\":%u,"
			  "\"collector_capacity\":%zu,\"window_us\":\"%" PRIu64 "\"}\n",
			  identities.producer_lifetime_id, identities.observer_lifetime_id, identities.clock_epoch,
			  ReceiptQueueDepth, ReceiptCapacity, window_us) >= 0;

	for (size_t i = 0; ok && i < receipt_count; ++i) { ok = writeReceipt(file, receipts[i]); }

	for (size_t i = 0; ok && i < summary_count; ++i) { ok = writeSummary(file, summaries[i]); }

	const char *fault = collectorFaultName(first_fault);
	ok = ok && fault && fprintf(file, "{\"type\":\"completion\",\"collector_fault\":\"%s\","
				    "\"receipt_count\":%zu,\"summary_count\":%zu,\"final_drain_complete\":%s}\n",
				    fault, receipt_count, summary_count, jsonBool(final_drain_complete)) >= 0;
	ok = ok && ferror(file) == 0;

	if (fclose(file) != 0) { ok = false; }

	return ok;
}

void reconcile()
{
	if (summary_count == 0) { fail(CollectorFault::MissingSummary); return; }

	if (summary_count != 1) { fail(CollectorFault::DuplicateSummary); return; }

	const auto &s = summaries[0].value;

	if (!s.finished || !s.has_state || s.first_fault != static_cast<uint8_t>(Fault::None)) {
		fail(CollectorFault::ProducerFailed);
		return;
	}

	if (receipt_count == 0) { fail(CollectorFault::FinalMismatch); return; }

	const auto &last = receipts[receipt_count - 1].value;

	if (s.producer_lifetime_id != identities.producer_lifetime_id
	    || s.observer_lifetime_id != identities.observer_lifetime_id || s.clock_epoch != identities.clock_epoch
	    || s.attempted_sequence != receipt_count || s.emitted_sequence != receipt_count
	    || last.sequence != s.emitted_sequence || last.transition_epoch != s.transition_epoch
	    || last.producer_time_us != s.producer_time_us || s.emitted_transition_epoch != s.transition_epoch
	    || s.emitted_producer_time_us != s.producer_time_us || s.final_time_us < s.producer_time_us
	    || s.final_time_us > summaries[0].receive_time_us
	    || last.after_arming_state != s.retained_arming_state || last.after_nav_state != s.retained_nav_state
	    || s.emitted_arming_state != s.retained_arming_state || s.emitted_nav_state != s.retained_nav_state
	    || s.actual_arming_state != s.retained_arming_state || s.actual_nav_state != s.retained_nav_state) {
		fail(CollectorFault::FinalMismatch);
	}
}

int finish(int argc)
{
	if (argc != 2 || phase != Phase::Collected || !captureProductionFinished()) {
		fail(CollectorFault::InvalidPhase);
		return 1; // Never offer evidence to a sink while production can continue.
	}

	phase = Phase::Finished;
	const bool drained = drain();
	bool receipt_updated{};
	bool summary_updated{};
	final_drain_complete = drained
			       && available(receipt_fd, receipt_updated, CollectorFault::ReceiptCopyFailed)
			       && available(summary_fd, summary_updated, CollectorFault::SummaryCopyFailed)
			       && !receipt_updated && !summary_updated;

	if (!final_drain_complete) { fail(CollectorFault::FinalMismatch); }

	reconcile();
	orb_unsubscribe(receipt_fd);
	orb_unsubscribe(summary_fd);
	receipt_fd = -1;
	summary_fd = -1;

	if (!writeEvidence()) { fail(CollectorFault::EvidenceWriteFailed); }

	return first_fault == CollectorFault::None ? 0 : 1;
}
} // namespace

extern "C" __EXPORT int research_state_capture_main(int argc, char *argv[])
{
	const std::lock_guard<std::mutex> lock(collector_mutex);

	if (argc >= 2 && strcmp(argv[1], "bootstrap") == 0) { return bootstrap(argc, argv); }

	if (argc >= 2 && strcmp(argv[1], "collect") == 0) { return collect(argc, argv); }

	if (argc >= 2 && strcmp(argv[1], "finish") == 0) { return finish(argc); }

	fail(CollectorFault::InvalidArguments);
	PX4_ERR("usage: research_state_capture bootstrap <producer> <observer> <clock> | collect <window_us> | finish");
	return 1;
}
