#pragma once

#include "NativeClosedDiagnosticOwner.hpp"

#if !defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) && !defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
#error "C1E ingress owner requires its private native or contract profile"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace research_sitl
{

inline constexpr char kNativeIngressOwnerProfile[] = "c1e-negative-native/v1";
inline constexpr size_t kNativeIngressSourceCapacity = 256;
inline constexpr size_t kNativeIngressTransactionCapacity = 32;

enum class NativeIngressPhase : uint8_t { Empty, Preparing, Ready, Processing, Resolved };
enum class NativeIngressAsyncFault : uint8_t { None, TransportFault, WorkerResourceExhausted };
enum class NativeIngressDispositionKind : uint8_t {
	Completed, DiscardedStale, DiscardedForeign, DiscardedClosed, Faulted
};
enum class NativeIngressTransactionKind : uint8_t { Completed, PollOnlyFault };
enum class NativeIngressCloseCause : uint8_t {
	OwnerStop, SourceContinuityLost, OwnerEvidenceExhausted, OwnerConfigurationInvalid,
	TransportFault, WorkerResourceExhausted, AdapterCapacityExhausted,
	AdapterFault, InvariantViolation
};
enum class NativeIngressFault : uint8_t {
	None, InvalidConfiguration, MissingInitial, RepeatedInitial, InvalidState,
	StateContradiction, TimeInvalid, TimeRegressed, CounterExhausted,
	OwnerEvidenceExhausted, WorkerResourceExhausted, AdapterCapacityExhausted, AdapterTerminal,
	AdapterUnavailable, SourceLifetimeLost, TransportFault, InvariantViolation
};

// The worker fills this only after full frame, signature, replay and immutable
// bootstrap checks. It owns any staged replay image separately from this slot.
struct NativeIngressCandidate {
	uint64_t token{};
	uint64_t native_run_id{}, owner_lifetime_id{}, controller_instance_id{};
	uint64_t source_id{}, transport_binding_id{}, request_sequence{};
	uint64_t expected_transition_epoch{}, producer_monotonic_us{}, producer_expiry_monotonic_us{};
	uint64_t clock_mapping_epoch{}, arrival_native_us{};
	uint64_t signing_timestamp_48{};
	float acceleration_x_frd_m_s2{}, acceleration_y_frd_m_s2{}, acceleration_z_frd_m_s2{};
	uint32_t protocol_version{};
	uint8_t domain{}, profile{}, target_system{}, target_component{};
	uint8_t frame{}, quantity{}, reference_point{}, flags{};
	uint8_t reserved[4] {};
	uint8_t channel{}, header_system{}, header_component{}, incoming_link_id{};
	uint8_t mavlink_sequence{}, payload_length{}, parser_stage{};
	uint16_t frame_crc{};
	uint8_t signature[13] {};
	uint16_t peer_port{};
};

// Exact W1 status scalars. The worker alone signs and serializes this value.
struct NativeIngressStatus {
	uint64_t native_run_id{}, owner_lifetime_id{}, controller_instance_id{}, source_id{};
	uint64_t transport_binding_id{}, request_sequence{}, status_sequence{}, transition_epoch{};
	uint64_t source_sequence{}, state_generation{}, arrival_native_us{}, evaluation_native_us{};
	uint64_t source_checked_native_us{}, clock_mapping_epoch{};
	uint32_t protocol_version{};
	uint16_t reason{};
	uint8_t domain{}, profile{}, execution_origin{}, outcome{};
	uint8_t arm_state{}, raw_nav_state{}, research_mode_state{}, selector_state{};
	uint8_t estimator_valid{}, clock_map_valid{}, eligible_for_control{}, flags{};
	uint8_t target_system{}, target_component{};
	uint8_t reserved[4] {};
};

struct NativeIngressDisposition {
	uint64_t token{};
	NativeIngressDispositionKind kind{NativeIngressDispositionKind::Faulted};
	bool has_actual_request{};
	bool has_status{};
	NativeIngressStatus status{};
};

// Fixed single-slot transfer. The worker owns Preparing/Ready/Resolved reads;
// Commander owns Processing and Resolved publication. Phase is the publication
// barrier for candidate/result bytes; neither side reads bytes it does not own.
struct NativeIngressMailbox {
	std::atomic<NativeIngressPhase> phase{NativeIngressPhase::Empty};
	std::atomic<NativeIngressAsyncFault> async_fault{NativeIngressAsyncFault::None};
	std::atomic<bool> terminal{false};
	NativeIngressCandidate candidate{};
	NativeIngressDisposition disposition{};
};

struct NativeIngressOwnerConfig {
	research_acceleration::ContractConfig contract{};
	research_acceleration::AdapterIdentity adapter_identity{};
	research_acceleration::NativeDiagnosticIdentity identity{};
	uint64_t transport_binding_id{}, producer_lifetime_id{}, source_clock_id{};
	uint8_t channel{};
	uint16_t peer_port{};
};

struct NativeIngressTransactionRecord {
	NativeIngressTransactionKind kind{NativeIngressTransactionKind::Completed};
	NativeIngressCandidate candidate{};
	NativeSourceSnapshot source{};
	research_acceleration::TrustedStateSample poll_input{};
	research_acceleration::DiagnosticResult poll_result{};
	bool has_request{};
	research_acceleration::TrustedStateSample request_input{};
	research_acceleration::DiagnosticResult request_result{};
	NativeIngressDisposition disposition{};
};

struct NativeIngressClosureRecord {
	NativeIngressCloseCause cause{NativeIngressCloseCause::OwnerConfigurationInvalid};
	NativeIngressFault fault{NativeIngressFault::None};
	std::optional<NativeSourceObservation> attempted;
	NativeSourceSnapshot retained_source{};
	std::optional<research_acceleration::TrustedStateSample> adapter_input;
	research_acceleration::DiagnosticResult result{};
};

struct NativeIngressOwnerSummary {
	NativeOwnerState state{NativeOwnerState::AwaitingInitial};
	NativeIngressFault fault{NativeIngressFault::None};
	NativeSourceSnapshot source{};
	uint16_t source_count{};
	uint8_t transaction_count{};
	uint64_t last_status_sequence{};
	bool has_closure{};
	bool evidence_complete{true};
};

class NativeIngressDiagnosticOwner final
{
public:
	NativeIngressDiagnosticOwner(const NativeIngressDiagnosticOwner &) = delete;
	NativeIngressDiagnosticOwner &operator=(const NativeIngressDiagnosticOwner &) = delete;
	NativeIngressDiagnosticOwner(NativeIngressDiagnosticOwner &&) = delete;
	NativeIngressDiagnosticOwner &operator=(NativeIngressDiagnosticOwner &&) = delete;
	NativeIngressOwnerSummary summary() const noexcept;
	bool copySource(size_t index, NativeSourceRecord &out) const noexcept;
	bool copyTransaction(size_t index, NativeIngressTransactionRecord &out) const noexcept;
	bool copyClosure(NativeIngressClosureRecord &out) const noexcept;

private:
	friend class NativeIngressSourceBridge;
#if defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
	friend struct NativeIngressOwnerTestPeer;
#endif
	explicit NativeIngressDiagnosticOwner(const NativeIngressOwnerConfig &config,
					      NativeIngressMailbox &mailbox) noexcept;
	void initial(RawState state, uint64_t hrt_us) noexcept;
	void transition(RawState before, RawState after, uint64_t hrt_us) noexcept;
	void sample(RawState state, uint64_t hrt_us) noexcept;
	void resolveReady(RawState current, uint64_t evaluation_hrt_us) noexcept;
	void stopOrFault(NativeIngressCloseCause cause, RawState current, uint64_t hrt_us) noexcept;
	void observe(const NativeSourceObservation &observation) noexcept;
	void close(NativeIngressCloseCause cause, NativeIngressFault fault,
		   const std::optional<NativeSourceObservation> &attempted,
		   const std::optional<research_acceleration::TrustedStateSample> &input,
		   uint64_t closing_hrt_us) noexcept;
	void discardReadyOnClosure(NativeIngressCloseCause cause, uint64_t hrt_us) noexcept;
	bool projectStatus(const NativeIngressCandidate &candidate, const NativeSourceSnapshot &source,
			   uint64_t evaluation_hrt_us, uint16_t reason, uint8_t outcome,
			   NativeIngressStatus &status) noexcept;
	bool closed() const noexcept;
	research_acceleration::TrustedStateSample physical(const NativeSourceSnapshot &source,
			uint64_t time_us) const noexcept;

	const NativeIngressOwnerConfig config_;
	NativeIngressMailbox &mailbox_;
	research_acceleration::WireAdapter adapter_;
	NativeSourceSnapshot source_{};
	NativeOwnerState state_{NativeOwnerState::AwaitingInitial};
	NativeIngressFault first_fault_{NativeIngressFault::None};
	std::array<NativeSourceRecord, kNativeIngressSourceCapacity> source_records_{};
	std::array<NativeIngressTransactionRecord, kNativeIngressTransactionCapacity> transactions_{};
	NativeIngressClosureRecord closure_{};
	uint16_t source_count_{};
	uint8_t transaction_count_{};
	uint64_t last_status_sequence_{};
	bool has_closure_{};
	bool evidence_complete_{true};
};

static_assert(sizeof(NativeIngressCandidate) <= 256, "C1E candidate exceeded its fixed slot budget");
static_assert(sizeof(NativeIngressDiagnosticOwner) <= 4U * 1024U * 1024U,
	      "C1E owner archive exceeds the host diagnostic memory cap");

} // namespace research_sitl
