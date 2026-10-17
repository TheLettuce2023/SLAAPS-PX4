#pragma once

#include "WireAdapter.hpp"

#if !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST) && !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
#error "Native closed owner requires an explicit native diagnostic profile"
#endif

#include <array>
#include <cstddef>
#include <optional>

namespace research_sitl
{

inline constexpr char kNativeClosedOwnerProfile[] = "c1d-closed-native/v1";
inline constexpr size_t kNativeSourceRecordCapacity = 256;

struct RawState { uint8_t arming_state{}; uint8_t nav_state{}; };
enum class NativeSourceKind : uint8_t { Initial, Transition, Sample, Stop };
struct NativeSourceObservation {
	NativeSourceKind kind{};
	RawState before{}, after{};
	uint64_t source_time_us{};
};
struct NativeSourceSnapshot {
	bool initialized{};
	RawState state{};
	uint64_t sequence{}, transition_epoch{}, physical_generation{};
	uint64_t committed_time_us{}, checked_time_us{};
};
enum class NativeOwnerState : uint8_t { AwaitingInitial, Observing, Stopped, Faulted };
enum class NativeOwnerFault : uint8_t {
	None, InvalidConfiguration, MissingInitial, RepeatedInitial, InvalidState,
	StateContradiction, TimeInvalid, TimeRegressed, CounterExhausted,
	EvidenceExhausted, AdapterTerminal, AdapterUnavailable, SourceLifetimeLost
};
struct NativeSourceRecord {
	NativeSourceObservation observation{};
	NativeSourceSnapshot before{}, after{};
	research_acceleration::TrustedStateSample adapter_input{};
	research_acceleration::DiagnosticResult result{};
};
struct NativeClosureRecord {
	NativeOwnerFault fault{NativeOwnerFault::None};
	std::optional<NativeSourceObservation> attempted;
	NativeSourceSnapshot retained_source{};
	std::optional<research_acceleration::TrustedStateSample> adapter_input;
	research_acceleration::DiagnosticResult result{};
};
struct NativeClosedOwnerConfig {
	research_acceleration::ContractConfig contract{};
	research_acceleration::AdapterIdentity adapter_identity{};
	research_acceleration::NativeDiagnosticIdentity identity{};
	uint64_t producer_lifetime_id{}, clock_epoch{};
};
struct NativeOwnerSummary {
	NativeOwnerState state{};
	NativeOwnerFault fault{};
	NativeSourceSnapshot source{};
	uint16_t record_count{};
	bool has_closure{};
};

// Single serialized source caller. Inspection requires permanent closure and
// quiescence. Large fixed storage belongs on the heap/static archive, not a task stack.
class NativeClosedDiagnosticOwner final
{
public:
	NativeClosedDiagnosticOwner(const NativeClosedDiagnosticOwner &) = delete;
	NativeClosedDiagnosticOwner &operator=(const NativeClosedDiagnosticOwner &) = delete;
	NativeClosedDiagnosticOwner(NativeClosedDiagnosticOwner &&) = delete;
	NativeClosedDiagnosticOwner &operator=(NativeClosedDiagnosticOwner &&) = delete;
	NativeOwnerSummary summary() const noexcept;
	bool copyRecord(size_t index, NativeSourceRecord &out) const noexcept;
	bool copyClosure(NativeClosureRecord &out) const noexcept;

private:
#if defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST)
	friend struct NativeClosedOwnerTestPeer;
#else
	friend class NativeClosedOwnerSourceBridge;
#endif
	explicit NativeClosedDiagnosticOwner(const NativeClosedOwnerConfig &config) noexcept;
	void initial(RawState state, uint64_t time_us) noexcept;
	void transition(RawState before, RawState after, uint64_t time_us) noexcept;
	void sample(RawState state, uint64_t time_us) noexcept;
	void stop(RawState state, uint64_t time_us) noexcept;
	void sourceContinuityLost() noexcept;
	void observe(const NativeSourceObservation &observation) noexcept;
	void fail(NativeOwnerFault fault, const std::optional<NativeSourceObservation> &attempted,
		  research_acceleration::DiagnosticCloseReason reason) noexcept;
	void finishAdapterClosure(NativeOwnerFault fault) noexcept;
	bool closed() const noexcept;
	research_acceleration::TrustedStateSample physical(const NativeSourceSnapshot &source, uint64_t time_us) const noexcept;

	const NativeClosedOwnerConfig config_;
	research_acceleration::WireAdapter adapter_;
	NativeSourceSnapshot source_{};
	NativeOwnerState state_{NativeOwnerState::AwaitingInitial};
	NativeOwnerFault first_fault_{NativeOwnerFault::None};
	std::array<NativeSourceRecord, kNativeSourceRecordCapacity> records_{};
	NativeClosureRecord closure_{};
	uint16_t record_count_{};
	bool has_closure_{};
};

static_assert(sizeof(NativeClosedDiagnosticOwner) <= 2U * 1024U * 1024U,
	      "Native diagnostic archive exceeds the host diagnostic memory cap");

} // namespace research_sitl
