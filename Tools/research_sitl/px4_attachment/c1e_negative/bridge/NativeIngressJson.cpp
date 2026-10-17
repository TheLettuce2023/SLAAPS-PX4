#include "NativeIngressJson.hpp"

#include "../../c1d_closed/bridge/NativeClosedJson.hpp"

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace research_sitl::native_ingress_json
{
namespace
{
using native_closed_json::Json;
using native_closed_json::array;
using native_closed_json::object;
using native_closed_json::value;

Json enumValue(NativeIngressTransactionKind kind)
{
	switch (kind) {
	case NativeIngressTransactionKind::Completed: return value("Completed");

	case NativeIngressTransactionKind::PollOnlyFault: return value("PollOnlyFault");
	}

	throw std::runtime_error("invalid C1E transaction kind");
}

Json enumValue(NativeIngressDispositionKind kind)
{
	switch (kind) {
	case NativeIngressDispositionKind::Completed: return value("Completed");

	case NativeIngressDispositionKind::DiscardedStale: return value("DiscardedStale");

	case NativeIngressDispositionKind::DiscardedForeign: return value("DiscardedForeign");

	case NativeIngressDispositionKind::DiscardedClosed: return value("DiscardedClosed");

	case NativeIngressDispositionKind::Faulted: return value("Faulted");
	}

	throw std::runtime_error("invalid C1E disposition kind");
}

Json enumValue(NativeIngressCloseCause cause)
{
	switch (cause) {
	case NativeIngressCloseCause::OwnerStop: return value("OwnerStop");

	case NativeIngressCloseCause::SourceContinuityLost: return value("SourceContinuityLost");

	case NativeIngressCloseCause::OwnerEvidenceExhausted: return value("OwnerEvidenceExhausted");

	case NativeIngressCloseCause::OwnerConfigurationInvalid: return value("OwnerConfigurationInvalid");

	case NativeIngressCloseCause::TransportFault: return value("TransportFault");

	case NativeIngressCloseCause::WorkerResourceExhausted: return value("WorkerResourceExhausted");

	case NativeIngressCloseCause::AdapterCapacityExhausted: return value("AdapterCapacityExhausted");

	case NativeIngressCloseCause::AdapterFault: return value("AdapterFault");

	case NativeIngressCloseCause::InvariantViolation: return value("InvariantViolation");
	}

	throw std::runtime_error("invalid C1E close cause");
}

Json enumValue(NativeIngressFault fault)
{
	switch (fault) {
	case NativeIngressFault::None: return value("None");

	case NativeIngressFault::InvalidConfiguration: return value("InvalidConfiguration");

	case NativeIngressFault::MissingInitial: return value("MissingInitial");

	case NativeIngressFault::RepeatedInitial: return value("RepeatedInitial");

	case NativeIngressFault::InvalidState: return value("InvalidState");

	case NativeIngressFault::StateContradiction: return value("StateContradiction");

	case NativeIngressFault::TimeInvalid: return value("TimeInvalid");

	case NativeIngressFault::TimeRegressed: return value("TimeRegressed");

	case NativeIngressFault::CounterExhausted: return value("CounterExhausted");

	case NativeIngressFault::OwnerEvidenceExhausted: return value("OwnerEvidenceExhausted");

	case NativeIngressFault::WorkerResourceExhausted: return value("WorkerResourceExhausted");

	case NativeIngressFault::AdapterCapacityExhausted: return value("AdapterCapacityExhausted");

	case NativeIngressFault::AdapterTerminal: return value("AdapterTerminal");

	case NativeIngressFault::AdapterUnavailable: return value("AdapterUnavailable");

	case NativeIngressFault::SourceLifetimeLost: return value("SourceLifetimeLost");

	case NativeIngressFault::TransportFault: return value("TransportFault");

	case NativeIngressFault::InvariantViolation: return value("InvariantViolation");
	}

	throw std::runtime_error("invalid C1E fault");
}

Json enumValue(DiagnosticStageCode code)
{
	switch (code) {
	case DiagnosticStageCode::Published: return value("Published");

	case DiagnosticStageCode::Busy: return value("Busy");

	case DiagnosticStageCode::Closed: return value("Closed");

	case DiagnosticStageCode::NotConfigured: return value("NotConfigured");

	case DiagnosticStageCode::MalformedEnvelope: return value("MalformedEnvelope");

	case DiagnosticStageCode::WrongMessage: return value("WrongMessage");

	case DiagnosticStageCode::WrongChannel: return value("WrongChannel");

	case DiagnosticStageCode::BadCrc: return value("BadCrc");

	case DiagnosticStageCode::BadSignature: return value("BadSignature");

	case DiagnosticStageCode::Replay: return value("Replay");

	case DiagnosticStageCode::WrongHeader: return value("WrongHeader");

	case DiagnosticStageCode::WrongLink: return value("WrongLink");

	case DiagnosticStageCode::InvalidPayload: return value("InvalidPayload");

	case DiagnosticStageCode::WrongIdentity: return value("WrongIdentity");

	case DiagnosticStageCode::WrongTarget: return value("WrongTarget");

	case DiagnosticStageCode::ResourceFault: return value("ResourceFault");
	}

	throw std::runtime_error("invalid C1E parser stage");
}

Json enumValue(NativeWorkerRejectKind kind)
{
	switch (kind) {
	case NativeWorkerRejectKind::Parser: return value("Parser");

	case NativeWorkerRejectKind::WrongPeer: return value("WrongPeer");

	case NativeWorkerRejectKind::ArrivalClock: return value("ArrivalClock");
	}

	throw std::runtime_error("invalid C1E worker reject kind");
}

Json enumValue(NativeWorkerSendState state)
{
	switch (state) {
	case NativeWorkerSendState::None: return value("None");

	case NativeWorkerSendState::Sent: return value("Sent");

	case NativeWorkerSendState::SignFailed: return value("SignFailed");

	case NativeWorkerSendState::SendFailed: return value("SendFailed");
	}

	throw std::runtime_error("invalid C1E worker send state");
}

Json byteArray(const uint8_t (&bytes)[4])
{
	return array({value(bytes[0]), value(bytes[1]), value(bytes[2]), value(bytes[3])});
}

Json signatureBytes(const uint8_t (&bytes)[13])
{
	std::vector<Json> result;
	result.reserve(13);

	for (uint8_t byte : bytes) { result.push_back(value(byte)); }

	return array(result);
}

Json candidate(const NativeIngressCandidate &v)
{
	return object({
		{"token", value(v.token)}, {"native_run_id", value(v.native_run_id)},
		{"owner_lifetime_id", value(v.owner_lifetime_id)},
		{"controller_instance_id", value(v.controller_instance_id)}, {"source_id", value(v.source_id)},
		{"transport_binding_id", value(v.transport_binding_id)}, {"request_sequence", value(v.request_sequence)},
		{"expected_transition_epoch", value(v.expected_transition_epoch)},
		{"producer_monotonic_us", value(v.producer_monotonic_us)},
		{"producer_expiry_monotonic_us", value(v.producer_expiry_monotonic_us)},
		{"clock_mapping_epoch", value(v.clock_mapping_epoch)}, {"arrival_native_us", value(v.arrival_native_us)},
		{"signing_timestamp_48", value(v.signing_timestamp_48)},
		{"acceleration_x_frd_m_s2", value(v.acceleration_x_frd_m_s2)},
		{"acceleration_y_frd_m_s2", value(v.acceleration_y_frd_m_s2)},
		{"acceleration_z_frd_m_s2", value(v.acceleration_z_frd_m_s2)},
		{"protocol_version", value(v.protocol_version)}, {"domain", value(v.domain)},
		{"profile", value(v.profile)}, {"target_system", value(v.target_system)},
		{"target_component", value(v.target_component)}, {"frame", value(v.frame)},
		{"quantity", value(v.quantity)}, {"reference_point", value(v.reference_point)},
		{"flags", value(v.flags)}, {"reserved", byteArray(v.reserved)}, {"channel", value(v.channel)},
		{"header_system", value(v.header_system)}, {"header_component", value(v.header_component)},
		{"incoming_link_id", value(v.incoming_link_id)}, {"mavlink_sequence", value(v.mavlink_sequence)},
		{"payload_length", value(v.payload_length)}, {"parser_stage", value(v.parser_stage)},
		{"peer_port", value(v.peer_port)}, {"frame_crc", value(v.frame_crc)},
		{"signature", signatureBytes(v.signature)}
	});
}

Json status(const NativeIngressStatus &v)
{
	return object({
		{"native_run_id", value(v.native_run_id)}, {"owner_lifetime_id", value(v.owner_lifetime_id)},
		{"controller_instance_id", value(v.controller_instance_id)}, {"source_id", value(v.source_id)},
		{"transport_binding_id", value(v.transport_binding_id)}, {"request_sequence", value(v.request_sequence)},
		{"status_sequence", value(v.status_sequence)}, {"transition_epoch", value(v.transition_epoch)},
		{"source_sequence", value(v.source_sequence)}, {"state_generation", value(v.state_generation)},
		{"arrival_native_us", value(v.arrival_native_us)}, {"evaluation_native_us", value(v.evaluation_native_us)},
		{"source_checked_native_us", value(v.source_checked_native_us)},
		{"clock_mapping_epoch", value(v.clock_mapping_epoch)}, {"protocol_version", value(v.protocol_version)},
		{"reason", value(v.reason)}, {"domain", value(v.domain)}, {"profile", value(v.profile)},
		{"execution_origin", value(v.execution_origin)}, {"outcome", value(v.outcome)},
		{"arm_state", value(v.arm_state)}, {"raw_nav_state", value(v.raw_nav_state)},
		{"research_mode_state", value(v.research_mode_state)}, {"selector_state", value(v.selector_state)},
		{"estimator_valid", value(v.estimator_valid)}, {"clock_map_valid", value(v.clock_map_valid)},
		{"eligible_for_control", value(v.eligible_for_control)}, {"flags", value(v.flags)},
		{"target_system", value(v.target_system)}, {"target_component", value(v.target_component)},
		{"reserved", byteArray(v.reserved)}
	});
}

Json disposition(const NativeIngressDisposition &v)
{
	return object({{"token", value(v.token)}, {"kind", enumValue(v.kind)},
		{"has_actual_request", value(v.has_actual_request)}, {"has_status", value(v.has_status)},
		{"status", status(v.status)}});
}

Json transaction(const NativeIngressTransactionRecord &v)
{
	return object({
		{"kind", enumValue(v.kind)}, {"candidate", candidate(v.candidate)}, {"source", value(v.source)},
		{"poll_input", value(v.poll_input)}, {"poll_result", value(v.poll_result)},
		{"has_request", value(v.has_request)}, {"request_input", value(v.request_input)},
		{"request_result", value(v.request_result)}, {"disposition", disposition(v.disposition)}
	});
}

Json closure(const NativeIngressClosureRecord &v)
{
	return object({
		{"cause", enumValue(v.cause)}, {"fault", enumValue(v.fault)},
		{"attempted", value(v.attempted)}, {"retained_source", value(v.retained_source)},
		{"adapter_input", value(v.adapter_input)}, {"result", value(v.result)}
	});
}

Json configuration(const NativeIngressOwnerConfig &v)
{
	return object({
		{"contract", value(v.contract)}, {"adapter_identity", value(v.adapter_identity)},
		{"identity", value(v.identity)}, {"transport_binding_id", value(v.transport_binding_id)},
		{"producer_lifetime_id", value(v.producer_lifetime_id)}, {"source_clock_id", value(v.source_clock_id)},
		{"channel", value(v.channel)}, {"peer_port", value(v.peer_port)}
	});
}

Json summary(const NativeIngressOwnerSummary &v)
{
	return object({
		{"state", value(v.state)}, {"fault", enumValue(v.fault)}, {"source", value(v.source)},
		{"source_count", value(v.source_count)}, {"transaction_count", value(v.transaction_count)},
		{"last_status_sequence", value(v.last_status_sequence)}, {"has_closure", value(v.has_closure)},
		{"evidence_complete", value(v.evidence_complete)}
	});
}

Json workerSummary(const NativeWorkerSummary &v)
{
	std::vector<Json> counts;
	counts.reserve(16);

	for (uint64_t count : v.reject_counts) { counts.push_back(value(count)); }

	return object({
		{"local_port", value(v.local_port)}, {"peer_port", value(v.peer_port)}, {"channel", value(v.channel)},
		{"reject_counts", array(counts)}, {"busy_drop_count", value(v.busy_drop_count)},
		{"wrong_peer_count", value(v.wrong_peer_count)}, {"arrival_clock_count", value(v.arrival_clock_count)},
		{"completed_replay_count", value(v.completed_replay_count)},
		{"discarded_replay_count", value(v.discarded_replay_count)},
		{"status_attempt_count", value(v.status_attempt_count)}, {"status_sent_count", value(v.status_sent_count)},
		{"status_failed_count", value(v.status_failed_count)},
		{"last_arrival_native_us", value(v.last_arrival_native_us)}, {"last_token", value(v.last_token)},
		{"reject_record_count", value(v.reject_record_count)},
		{"delivery_record_count", value(v.delivery_record_count)},
		{"last_send_state", enumValue(v.last_send_state)}, {"retired", value(v.retired)}
	});
}

Json rejectRecord(const NativeWorkerRejectRecord &v)
{
	return object({
		{"kind", enumValue(v.kind)}, {"stage", enumValue(v.stage)},
		{"arrival_native_us", value(v.arrival_native_us)}, {"peer_port", value(v.peer_port)}
	});
}

Json deliveryRecord(const NativeWorkerDeliveryRecord &v)
{
	return object({
		{"token", value(v.token)}, {"status_sequence", value(v.status_sequence)},
		{"signing_timestamp_48", value(v.signing_timestamp_48)},
		{"disposition", enumValue(v.disposition)}, {"send_state", enumValue(v.send_state)},
		{"signed_size", value(v.signed_size)}, {"replay_committed", value(v.replay_committed)},
		{"has_status", value(v.has_status)}
	});
}

Json lifecycleValue(const Lifecycle &v)
{
	return object({
		{"bootstrap_count", value(v.bootstrap_count)}, {"claim_count", value(v.claim_count)},
		{"stack_size_bytes", value(v.stack_size_bytes)}, {"stack_guard_bytes", value(v.stack_guard_bytes)},
		{"stack_marker_offset_bytes", value(v.stack_marker_offset_bytes)},
		{"observation_window_us", value(v.observation_window_us)},
		{"commander_stop_result", value(static_cast<int64_t>(v.commander_stop_result))},
		{"bridge_fault", value(v.bridge_fault)}, {"source_done", value(v.source_done)},
		{"worker_joined", value(v.worker_joined)}, {"stack_measured", value(v.stack_measured)},
		{"waited", value(v.waited)}, {"wait_completed", value(v.wait_completed)}
	});
}

bool writeExclusive(const char *path, const Json &document) noexcept
{
	if (!path || !*path) { return false; }

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

} // namespace

bool writeArchive(const char *path, const NativeIngressDiagnosticOwner &owner,
		  const NativeIngressWorker &worker, const NativeIngressOwnerConfig &config,
		  const Lifecycle &lifecycle)
{
	try {
		if (!lifecycle.source_done || !lifecycle.worker_joined || !lifecycle.stack_measured
		    || lifecycle.bootstrap_count != 1 || lifecycle.claim_count != 1
		    || lifecycle.commander_stop_result != 0) { return false; }

		native_closed_json::reset();
		const NativeIngressOwnerSummary owner_summary = owner.summary();
		const NativeWorkerSummary worker_summary = worker.summary();

		if (!worker_summary.retired || owner_summary.source_count > kNativeIngressSourceCapacity
		    || owner_summary.transaction_count > kNativeIngressTransactionCapacity
		    || worker_summary.reject_record_count > 64 || worker_summary.delivery_record_count > 33) { return false; }

		const std::unique_ptr<NativeSourceRecord> source(new (std::nothrow) NativeSourceRecord{});
		const std::unique_ptr<NativeIngressTransactionRecord> transaction_record(
			new (std::nothrow) NativeIngressTransactionRecord{});
		const std::unique_ptr<NativeIngressClosureRecord> closure_record(
			new (std::nothrow) NativeIngressClosureRecord{});

		if (!source || !transaction_record || !closure_record) { return false; }

		std::vector<Json> sources;
		std::vector<Json> transactions;
		std::vector<Json> rejections;
		std::vector<Json> deliveries;
		sources.reserve(owner_summary.source_count);
		transactions.reserve(owner_summary.transaction_count);
		rejections.reserve(worker_summary.reject_record_count);
		deliveries.reserve(worker_summary.delivery_record_count);
		uint32_t report_count{};

		for (size_t index = 0; index < owner_summary.source_count; ++index) {
			if (!owner.copySource(index, *source) || source->result.record_count > 4) { return false; }

			report_count += source->result.record_count;
			sources.push_back(value(*source));
		}

		for (size_t index = 0; index < owner_summary.transaction_count; ++index) {
			if (!owner.copyTransaction(index, *transaction_record)
			    || transaction_record->poll_result.record_count > 4
			    || transaction_record->request_result.record_count > 4) { return false; }

			report_count += transaction_record->poll_result.record_count;

			if (transaction_record->has_request) { report_count += transaction_record->request_result.record_count; }

			transactions.push_back(transaction(*transaction_record));
		}

		Json closure_value = "null";
		uint64_t last_adapter_report_sequence{};

		if (owner_summary.has_closure) {
			if (!owner.copyClosure(*closure_record) || closure_record->result.record_count > 4) { return false; }

			report_count += closure_record->result.record_count;
			last_adapter_report_sequence = closure_record->result.snapshot.adapter.last_report_sequence;
			closure_value = closure(*closure_record);
		}

		for (size_t index = 0; index < worker_summary.reject_record_count; ++index) {
			NativeWorkerRejectRecord record{};

			if (!worker.copyReject(index, record)) { return false; }

			rejections.push_back(rejectRecord(record));
		}

		for (size_t index = 0; index < worker_summary.delivery_record_count; ++index) {
			NativeWorkerDeliveryRecord record{};

			if (!worker.copyDelivery(index, record)) { return false; }

			deliveries.push_back(deliveryRecord(record));
		}

		const Json document = object({
			{"schema", value("research-c1e-native-ingress/v1")},
			{
				"header", object({
					{"profile_id", value(kNativeIngressOwnerProfile)},
					{"execution_origin", value("NativePosixSitl")},
					{"clock_source", value("px4_hrt_absolute_time_native_posix")},
					{"configuration", configuration(config)},
					{"source_capacity", value(static_cast<uint16_t>(kNativeIngressSourceCapacity))},
					{"transaction_capacity", value(static_cast<uint8_t>(kNativeIngressTransactionCapacity))},
					{"preauth_reject_capacity", value(static_cast<uint8_t>(64))},
					{"owner_size_bytes", value(static_cast<uint64_t>(sizeof(NativeIngressDiagnosticOwner)))},
					{"worker_size_bytes", value(static_cast<uint64_t>(sizeof(NativeIngressWorker)))}
				})
			},
			{"source_records", array(sources)}, {"transactions", array(transactions)},
			{"closure", closure_value}, {"summary", summary(owner_summary)},
			{
				"worker", object({{"summary", workerSummary(worker_summary)},
					{"rejections", array(rejections)}, {"deliveries", array(deliveries)}})
			},
			{"lifecycle", lifecycleValue(lifecycle)},
			{
				"footer", object({
					{"source_count", value(owner_summary.source_count)},
					{"transaction_count", value(owner_summary.transaction_count)},
					{"adapter_report_count", value(report_count)},
					{"last_adapter_report_sequence", value(last_adapter_report_sequence)},
					{"last_status_sequence", value(owner_summary.last_status_sequence)},
					{"completed_replay_count", value(worker_summary.completed_replay_count)},
					{"status_sent_count", value(worker_summary.status_sent_count)}
				})
			}
		}) + '\n';

		if (!native_closed_json::valid() || !writeExclusive(path, document)) { return false; }

		return owner_summary.state == NativeOwnerState::Stopped
		       && owner_summary.fault == NativeIngressFault::None && owner_summary.has_closure
		       && owner_summary.evidence_complete && worker_summary.status_failed_count == 0
		       && lifecycle.bridge_fault == 0 && lifecycle.waited && lifecycle.wait_completed;

	} catch (...) {
		return false;
	}
}

} // namespace research_sitl::native_ingress_json
