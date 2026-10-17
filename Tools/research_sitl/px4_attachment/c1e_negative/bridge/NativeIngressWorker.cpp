#include "NativeIngressWorker.hpp"

#include <drivers/drv_hrt.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace research_sitl
{
namespace
{
constexpr uint64_t kSigningClockDivisor = 10; // MAVLink signing units are 10 us.
constexpr size_t kRejectCapacity = 64;
static_assert(static_cast<size_t>(DiagnosticStageCode::ResourceFault) + 1 == 16, "update rejection counts");

bool loopbackPeer(const sockaddr_in &source, socklen_t size, uint16_t port) noexcept
{
	return size == sizeof(sockaddr_in) && source.sin_family == AF_INET
	       && source.sin_addr.s_addr == htonl(INADDR_LOOPBACK) && source.sin_port == htons(port);
}
} // namespace

NativeIngressWorker::NativeIngressWorker(const DiagnosticParserConfig &config, NativeIngressMailbox &mailbox,
		uint16_t local_port, uint16_t peer_port) noexcept :
	mailbox_(mailbox), parser_(config), channel_(config.channel), local_port_(local_port), peer_port_(peer_port)
{
	summary_.local_port = local_port_;
	summary_.peer_port = peer_port_;
	summary_.channel = channel_;
}

NativeIngressWorker::~NativeIngressWorker()
{
	retire();
}

void NativeIngressWorker::publishFault(NativeIngressAsyncFault fault) noexcept
{
	auto expected = NativeIngressAsyncFault::None;
	mailbox_.async_fault.compare_exchange_strong(expected, fault, std::memory_order_release,
			std::memory_order_relaxed);
}

bool NativeIngressWorker::checkedIncrement(uint64_t &counter) noexcept
{
	if (counter == std::numeric_limits<uint64_t>::max()) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		return false;
	}

	++counter;
	return true;
}

void NativeIngressWorker::reject(NativeWorkerRejectKind kind, DiagnosticStageCode stage,
				 uint64_t arrival, uint16_t peer_port) noexcept
{
	if (kind == NativeWorkerRejectKind::Parser) {
		const auto index = static_cast<size_t>(stage);

		if (index >= 16 || !checkedIncrement(summary_.reject_counts[index])) { return; }

	} else if (kind == NativeWorkerRejectKind::WrongPeer && !checkedIncrement(summary_.wrong_peer_count)) {
		return;

	} else if (kind == NativeWorkerRejectKind::ArrivalClock && !checkedIncrement(summary_.arrival_clock_count)) {
		return;
	}

	if (summary_.reject_record_count == kRejectCapacity) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		return;
	}

	rejects_[summary_.reject_record_count++] = {kind, stage, arrival, peer_port};
}

bool NativeIngressWorker::start() noexcept
{
	if (!parser_.configured() || local_port_ == 0 || peer_port_ == 0 || local_port_ == peer_port_
	    || socket_fd_ >= 0 || summary_.retired) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		return false;
	}

	const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	if (fd < 0) { publishFault(NativeIngressAsyncFault::TransportFault); return false; }

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(local_port_);

	if (bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
		close(fd);
		publishFault(NativeIngressAsyncFault::TransportFault);
		return false;
	}

	socket_fd_ = fd;
	return true;
}

void NativeIngressWorker::mapCandidate(const DiagnosticStageResult &stage, uint64_t arrival) noexcept
{
	const auto &request = stage.request;
	auto &candidate = mailbox_.candidate;
	candidate = {};
	candidate.token = stage.token;
	candidate.native_run_id = request.native_run_id;
	candidate.owner_lifetime_id = request.owner_lifetime_id;
	candidate.controller_instance_id = request.controller_instance_id;
	candidate.source_id = request.source_id;
	candidate.transport_binding_id = request.transport_session_id;
	candidate.request_sequence = request.request_sequence;
	candidate.expected_transition_epoch = request.expected_transition_epoch;
	candidate.producer_monotonic_us = request.producer_monotonic_us;
	candidate.producer_expiry_monotonic_us = request.producer_expiry_monotonic_us;
	candidate.clock_mapping_epoch = request.clock_mapping_epoch;
	candidate.arrival_native_us = arrival;
	candidate.signing_timestamp_48 = stage.frame.signing_timestamp;
	candidate.acceleration_x_frd_m_s2 = request.acceleration_x_frd_m_s2;
	candidate.acceleration_y_frd_m_s2 = request.acceleration_y_frd_m_s2;
	candidate.acceleration_z_frd_m_s2 = request.acceleration_z_frd_m_s2;
	candidate.protocol_version = request.protocol_version;
	candidate.domain = request.domain;
	candidate.profile = request.profile;
	candidate.target_system = request.target_system;
	candidate.target_component = request.target_component;
	candidate.frame = request.frame;
	candidate.quantity = request.quantity;
	candidate.reference_point = request.reference_point;
	candidate.flags = request.flags;
	std::memcpy(candidate.reserved, request.reserved, sizeof(candidate.reserved));
	candidate.channel = stage.frame.channel;
	candidate.header_system = stage.frame.system;
	candidate.header_component = stage.frame.component;
	candidate.incoming_link_id = stage.frame.link_id;
	candidate.mavlink_sequence = stage.frame.sequence;
	candidate.payload_length = stage.frame.payload_length;
	candidate.parser_stage = 1; // Set only by a Published authenticated parser result.
	candidate.peer_port = peer_port_;
	candidate.frame_crc = stage.frame.checksum;
	std::memcpy(candidate.signature, stage.frame.signature, sizeof(candidate.signature));
}

bool NativeIngressWorker::sendStatus(const NativeIngressStatus &status,
				     NativeWorkerDeliveryRecord &delivery) noexcept
{
	if (!checkedIncrement(summary_.status_attempt_count)) { return false; }

	mavlink_research_diagnostic_status_t wire{};
	wire.native_run_id = status.native_run_id;
	wire.owner_lifetime_id = status.owner_lifetime_id;
	wire.controller_instance_id = status.controller_instance_id;
	wire.source_id = status.source_id;
	wire.transport_session_id = status.transport_binding_id;
	wire.request_sequence = status.request_sequence;
	wire.status_sequence = status.status_sequence;
	wire.transition_epoch = status.transition_epoch;
	wire.source_sequence = status.source_sequence;
	wire.state_generation = status.state_generation;
	wire.arrival_native_us = status.arrival_native_us;
	wire.evaluation_native_us = status.evaluation_native_us;
	wire.source_checked_native_us = status.source_checked_native_us;
	wire.clock_mapping_epoch = status.clock_mapping_epoch;
	wire.protocol_version = status.protocol_version;
	wire.reason = status.reason;
	wire.domain = status.domain;
	wire.profile = status.profile;
	wire.execution_origin = status.execution_origin;
	wire.outcome = status.outcome;
	wire.arm_state = status.arm_state;
	wire.raw_nav_state = status.raw_nav_state;
	wire.research_mode_state = status.research_mode_state;
	wire.selector_state = status.selector_state;
	wire.estimator_valid = status.estimator_valid;
	wire.clock_map_valid = status.clock_map_valid;
	wire.eligible_for_control = status.eligible_for_control;
	wire.flags = status.flags;
	wire.target_system = status.target_system;
	wire.target_component = status.target_component;
	std::memcpy(wire.reserved, status.reserved, sizeof(wire.reserved));
	uint8_t frame[MAVLINK_MAX_PACKET_LEN] {};
	size_t size{};
	const uint64_t now = hrt_absolute_time();

	if (now == 0 || !parser_.signStatus(wire, now / kSigningClockDivisor, frame, sizeof(frame), size)) {
		summary_.last_send_state = NativeWorkerSendState::SignFailed;
		delivery.send_state = NativeWorkerSendState::SignFailed;
		checkedIncrement(summary_.status_failed_count);
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		return false;
	}

	delivery.signed_size = static_cast<uint16_t>(size);
	delivery.signing_timestamp_48 = 0;

	for (size_t index = 0; index < 6; ++index) {
		delivery.signing_timestamp_48 |= uint64_t(frame[size - MAVLINK_SIGNATURE_BLOCK_LEN + 1 + index])
						 << (index * 8);
	}

	sockaddr_in peer{};
	peer.sin_family = AF_INET;
	peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	peer.sin_port = htons(peer_port_);
	const ssize_t written = sendto(socket_fd_, frame, size, MSG_DONTWAIT,
				       reinterpret_cast<const sockaddr *>(&peer), sizeof(peer));

	if (written != static_cast<ssize_t>(size)) {
		summary_.last_send_state = NativeWorkerSendState::SendFailed;
		delivery.send_state = NativeWorkerSendState::SendFailed;
		checkedIncrement(summary_.status_failed_count);
		publishFault(NativeIngressAsyncFault::TransportFault);
		return false;
	}

	summary_.last_send_state = NativeWorkerSendState::Sent;
	delivery.send_state = NativeWorkerSendState::Sent;
	checkedIncrement(summary_.status_sent_count);
	return true;
}

void NativeIngressWorker::clearSlot() noexcept
{
	staged_token_ = 0;
	mailbox_.phase.store(NativeIngressPhase::Empty, std::memory_order_release);
}

void NativeIngressWorker::consumeResolved() noexcept
{
	const NativeIngressDisposition disposition = mailbox_.disposition;

	if (staged_token_ == 0 || disposition.token != staged_token_) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		parser_.discardPending(staged_token_);
		clearSlot();
		return;
	}

	const bool completed = disposition.kind == NativeIngressDispositionKind::Completed
			       && disposition.has_actual_request;

	if (disposition.kind == NativeIngressDispositionKind::Completed && !disposition.has_actual_request) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
	}

	const bool replay_ok = completed ? parser_.commitPending(staged_token_)
			       : parser_.discardPending(staged_token_);

	if (!replay_ok) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		clearSlot();
		return;
	}

	if (completed) {
		checkedIncrement(summary_.completed_replay_count);

	} else {
		checkedIncrement(summary_.discarded_replay_count);
	}

	NativeWorkerDeliveryRecord delivery{};
	delivery.token = disposition.token;
	delivery.disposition = disposition.kind;
	delivery.replay_committed = completed;
	delivery.has_status = disposition.has_status;
	delivery.status_sequence = disposition.has_status ? disposition.status.status_sequence : 0;

	if (disposition.has_status && (disposition.kind != NativeIngressDispositionKind::Completed || completed)) {
		sendStatus(disposition.status, delivery);
	}

	if (summary_.delivery_record_count == deliveries_.size()) {
		publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);

	} else {
		deliveries_[summary_.delivery_record_count++] = delivery;
	}

	clearSlot();
}

bool NativeIngressWorker::pumpOnce() noexcept
{
	const auto phase = mailbox_.phase.load(std::memory_order_acquire);

	if (phase == NativeIngressPhase::Resolved) { consumeResolved(); return true; }

	if (mailbox_.terminal.load(std::memory_order_acquire)) {
		if (phase == NativeIngressPhase::Ready) {
			auto expected = NativeIngressPhase::Ready;

			if (mailbox_.phase.compare_exchange_strong(expected, NativeIngressPhase::Empty,
					std::memory_order_acq_rel, std::memory_order_acquire)) {
				if (!parser_.discardPending(staged_token_)) {
					publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
				}

				checkedIncrement(summary_.discarded_replay_count);
				staged_token_ = 0;
				return true;
			}
		}

		return false;
	}

	if (socket_fd_ < 0 || mailbox_.async_fault.load(std::memory_order_acquire) != NativeIngressAsyncFault::None) {
		return false;
	}

	uint8_t datagram[MAVLINK_MAX_PACKET_LEN + 1] {};
	sockaddr_in source{};
	socklen_t source_size = sizeof(source);
	const ssize_t received = recvfrom(socket_fd_, datagram, sizeof(datagram), MSG_DONTWAIT | MSG_TRUNC,
					  reinterpret_cast<sockaddr *>(&source), &source_size);

	if (received < 0) {
		if (errno == EAGAIN || errno == EINTR) { return false; }

#if EWOULDBLOCK != EAGAIN

		if (errno == EWOULDBLOCK) { return false; }

#endif

		publishFault(NativeIngressAsyncFault::TransportFault);
		return false;
	}

	const uint64_t arrival = hrt_absolute_time();

	if (arrival == 0 || arrival < summary_.last_arrival_native_us) {
		reject(NativeWorkerRejectKind::ArrivalClock, DiagnosticStageCode::ResourceFault, arrival,
		       ntohs(source.sin_port));
		publishFault(NativeIngressAsyncFault::TransportFault);
		return true;
	}

	summary_.last_arrival_native_us = arrival;

	if (phase != NativeIngressPhase::Empty) {
		checkedIncrement(summary_.busy_drop_count);
		return true;
	}

	if (mailbox_.terminal.load(std::memory_order_acquire)) { return true; }

	if (!loopbackPeer(source, source_size, peer_port_)) {
		reject(NativeWorkerRejectKind::WrongPeer, DiagnosticStageCode::WrongIdentity,
		       arrival, ntohs(source.sin_port));
		return true;
	}

	if (received > MAVLINK_MAX_PACKET_LEN) {
		reject(NativeWorkerRejectKind::Parser, DiagnosticStageCode::MalformedEnvelope,
		       arrival, peer_port_);
		return true;
	}

	auto expected = NativeIngressPhase::Empty;

	if (!mailbox_.phase.compare_exchange_strong(expected, NativeIngressPhase::Preparing,
			std::memory_order_acq_rel, std::memory_order_acquire)) {
		checkedIncrement(summary_.busy_drop_count);
		return true;
	}

	const DiagnosticStageResult stage = parser_.stageDatagram({datagram, static_cast<size_t>(received)}, channel_);

	if (stage.code != DiagnosticStageCode::Published) {
		reject(NativeWorkerRejectKind::Parser, stage.code, arrival, peer_port_);

		if (stage.code == DiagnosticStageCode::ResourceFault || stage.code == DiagnosticStageCode::Busy
		    || stage.code == DiagnosticStageCode::Closed || stage.code == DiagnosticStageCode::NotConfigured) {
			publishFault(NativeIngressAsyncFault::WorkerResourceExhausted);
		}

		mailbox_.phase.store(NativeIngressPhase::Empty, std::memory_order_release);
		return true;
	}

	staged_token_ = stage.token;
	summary_.last_token = stage.token;

	if (mailbox_.terminal.load(std::memory_order_acquire)) {
		parser_.discardPending(staged_token_);
		checkedIncrement(summary_.discarded_replay_count);
		clearSlot();
		return true;
	}

	mapCandidate(stage, arrival);
	mailbox_.phase.store(NativeIngressPhase::Ready, std::memory_order_release);

	if (mailbox_.terminal.load(std::memory_order_acquire)) {
		expected = NativeIngressPhase::Ready;

		if (mailbox_.phase.compare_exchange_strong(expected, NativeIngressPhase::Empty,
				std::memory_order_acq_rel, std::memory_order_acquire)) {
			parser_.discardPending(staged_token_);
			checkedIncrement(summary_.discarded_replay_count);
			staged_token_ = 0;
		}
	}

	return true;
}

void NativeIngressWorker::retire() noexcept
{
	if (summary_.retired) { return; }

	if (staged_token_ != 0) { parser_.discardPending(staged_token_); }

	parser_.retire();

	if (socket_fd_ >= 0) { close(socket_fd_); socket_fd_ = -1; }

	summary_.retired = true;
}

bool NativeIngressWorker::copyReject(size_t index, NativeWorkerRejectRecord &out) const noexcept
{
	if (index >= summary_.reject_record_count) { return false; }

	out = rejects_[index];
	return true;
}

bool NativeIngressWorker::copyDelivery(size_t index, NativeWorkerDeliveryRecord &out) const noexcept
{
	if (index >= summary_.delivery_record_count) { return false; }

	out = deliveries_[index];
	return true;
}

} // namespace research_sitl
