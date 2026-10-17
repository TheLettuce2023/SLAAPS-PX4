#pragma once
#include "NativeClosedDiagnosticOwner.hpp"
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
namespace research_sitl
{
namespace native_closed_json
{
using namespace research_acceleration;
using Json = std::string;
// Post-retirement serialization only. Failure is sticky; no numeric enum fallback.
void reset() noexcept;
bool valid() noexcept;
Json value(const std::string &text);
Json value(const char *text);
Json value(bool v);
Json value(uint8_t v);
Json value(uint16_t v);
Json value(uint32_t v);
Json value(uint64_t v);
Json value(int64_t v);
Json value(float v);
Json object(std::initializer_list<std::pair<std::string, Json>> members);
Json array(const std::vector<Json> &items);
Json value(const Vec3f &v);
Json value(ArmState v);
Json value(ResearchModeState v);
Json value(DisarmedPolicy v);
Json value(DecisionCode v);
Json value(SessionGate v);
Json value(AdminCode v);
Json value(IngressCode v);
Json value(IngressPacketKind v);
Json value(DiagnosticDomain v);
Json value(DiagnosticCloseReason v);
Json value(DiagnosticReportKind v);
Json value(DiagnosticOperationKind v);
Json value(DiagnosticSelectionState v);
Json value(DiagnosticCode v);
Json value(DiagnosticLocalReason v);
Json value(const TrustedSession &v);
Json value(const TrustedClockMap &v);
Json value(const EvaluationContext &v);
Json value(const TrustedStateSample &v);
Json value(const Decision &v);
Json value(const AccelerationBounds &v);
Json value(const TimingRequirements &v);
Json value(const ContractConfig &v);
Json value(const AdapterIdentity &v);
Json value(const AdapterSnapshot &v);
Json value(const SessionSnapshot &v);
Json value(const AdmissionSnapshot &v);
Json value(const SessionIngressVerdict &v);
Json value(const DiagnosticSnapshot &v);
Json value(const IngressCorrelation &v);
Json value(const AdminCorrelation &v);
Json value(const DiagnosticCorrelation &v);
Json value(const DiagnosticReport &v);
Json value(const DiagnosticCause &v);
Json value(const DiagnosticResult &v);
Json value(DiagnosticExecutionOrigin v);
Json value(NativeSourceKind v);
Json value(NativeOwnerState v);
Json value(NativeOwnerFault v);
Json value(const NativeDiagnosticIdentity &v);
Json value(const RawState &v);
Json value(const NativeSourceObservation &v);
Json value(const NativeSourceSnapshot &v);
Json value(const NativeSourceRecord &v);
Json value(const NativeClosureRecord &v);
Json value(const NativeClosedOwnerConfig &v);
Json value(const NativeOwnerSummary &v);
template<typename T>
Json value(const std::optional<T> &v) { return v ? value(*v) : "null"; }
}
}
