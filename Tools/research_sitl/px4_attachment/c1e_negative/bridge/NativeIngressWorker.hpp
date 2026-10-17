#pragma once

#include <lib/research_acceleration/NativeIngressDiagnosticOwner.hpp>
#include "../../../diagnostic_wire/native_parser/DiagnosticSignedMavlink.hpp"

#if !defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
#error "C1E endpoint worker requires the private native POSIX target"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace research_sitl
{

enum class NativeWorkerRejectKind : uint8_t { Parser, WrongPeer, ArrivalClock };
struct NativeWorkerRejectRecord {
	NativeWorkerRejectKind kind{NativeWorkerRejectKind::Parser};
	DiagnosticStageCode stage{DiagnosticStageCode::NotConfigured};
	uint64_t arrival_native_us{};
	uint16_t peer_port{};
};

enum class NativeWorkerSendState : uint8_t { None, Sent, SignFailed, SendFailed };
struct NativeWorkerDeliveryRecord {
	uint64_t token{};
	uint64_t status_sequence{};
	uint64_t signing_timestamp_48{};
	NativeIngressDispositionKind disposition{NativeIngressDispositionKind::Faulted};
	NativeWorkerSendState send_state{NativeWorkerSendState::None};
	uint16_t signed_size{};
	bool replay_committed{};
	bool has_status{};
};
struct NativeWorkerSummary {
	uint16_t local_port{};
	uint16_t peer_port{};
	uint8_t channel{};
	uint64_t reject_counts[16] {};
	uint64_t busy_drop_count{};
	uint64_t wrong_peer_count{};
	uint64_t arrival_clock_count{};
	uint64_t completed_replay_count{};
	uint64_t discarded_replay_count{};
	uint64_t status_attempt_count{};
	uint64_t status_sent_count{};
	uint64_t status_failed_count{};
	uint64_t last_arrival_native_us{};
	uint64_t last_token{};
	uint16_t reject_record_count{};
	uint8_t delivery_record_count{};
	NativeWorkerSendState last_send_state{NativeWorkerSendState::None};
	bool retired{};
};

class NativeIngressWorker final
{
public:
	NativeIngressWorker(const DiagnosticParserConfig &config, NativeIngressMailbox &mailbox,
			    uint16_t local_port, uint16_t peer_port) noexcept;
	~NativeIngressWorker();
	NativeIngressWorker(const NativeIngressWorker &) = delete;
	NativeIngressWorker &operator=(const NativeIngressWorker &) = delete;
	bool start() noexcept;
	// Called only on the worker thread; at most one receive and one slot action.
	bool pumpOnce() noexcept;
	void requestStop() noexcept { stop_requested_.store(true, std::memory_order_release); }
	bool stopRequested() const noexcept { return stop_requested_.load(std::memory_order_acquire); }
	void retire() noexcept;
	NativeWorkerSummary summary() const noexcept { return summary_; } // after joined retirement only
	bool copyReject(size_t index, NativeWorkerRejectRecord &out) const noexcept;
	bool copyDelivery(size_t index, NativeWorkerDeliveryRecord &out) const noexcept;

private:
	void publishFault(NativeIngressAsyncFault fault) noexcept;
	bool checkedIncrement(uint64_t &counter) noexcept;
	void reject(NativeWorkerRejectKind kind, DiagnosticStageCode stage, uint64_t arrival,
		    uint16_t peer_port) noexcept;
	void clearSlot() noexcept;
	void consumeResolved() noexcept;
	void mapCandidate(const DiagnosticStageResult &stage, uint64_t arrival) noexcept;
	bool sendStatus(const NativeIngressStatus &status, NativeWorkerDeliveryRecord &delivery) noexcept;

	NativeIngressMailbox &mailbox_;
	DiagnosticSignedMavlink parser_;
	const uint8_t channel_;
	const uint16_t local_port_;
	const uint16_t peer_port_;
	int socket_fd_{-1};
	std::atomic<bool> stop_requested_{false};
	uint64_t staged_token_{};
	NativeWorkerSummary summary_{};
	std::array<NativeWorkerRejectRecord, 64> rejects_{};
	std::array<NativeWorkerDeliveryRecord, 33> deliveries_{};
};

} // namespace research_sitl
