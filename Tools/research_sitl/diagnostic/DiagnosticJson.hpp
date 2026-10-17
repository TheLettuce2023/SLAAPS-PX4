#pragma once

#include "HarnessSupervisorOwner.hpp"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace research_sitl
{
namespace diagnostic_json
{
using namespace research_acceleration;

// All returned strings are complete JSON values. Unknown enums and nonfinite
// floats throw; there is no numeric fallback or partial successful artifact.
using Json = std::string;
Json object(std::initializer_list<std::pair<std::string, Json>> members);
Json array(const std::vector<Json> &items);
Json value(const std::string &text);
Json value(const char *text);
Json value(bool v);
Json value(uint8_t v);
Json value(uint16_t v);
Json value(uint32_t v);
Json value(uint64_t v);
Json value(int64_t v);
Json value(float v);
Json value(const Vec3f &v);
Json value(ArmState v);
Json value(ResearchModeState v);
Json value(DisarmedPolicy v);
Json value(Frame v);
Json value(Quantity v);
Json value(ReferencePoint v);
Json value(DecisionCode v);
Json value(SessionGate v);
Json value(StatusDecisionOrigin v);
Json value(AdapterCode v);
Json value(AdapterReportKind v);
Json value(SupervisorAction v);
Json value(AdminCode v);
Json value(IngressCode v);
Json value(IngressPacketKind v);
Json value(DiagnosticDomain v);
Json value(DiagnosticAction v);
Json value(DiagnosticCloseReason v);
Json value(DiagnosticReportKind v);
Json value(DiagnosticOperationKind v);
Json value(DiagnosticSelectionState v);
Json value(DiagnosticCode v);
Json value(DiagnosticLocalReason v);
Json value(Intent v);
Json value(ConsoleDisposition v);
Json value(OwnerOutcome v);
Json value(BrokerCode v);
Json value(const TrustedSession &v);
Json value(const TrustedClockMap &v);
Json value(const EvaluationContext &v);
Json value(const TrustedStateSample &v);
Json value(const Decision &v);
Json value(const AccelerationBounds &v);
Json value(const TimingRequirements &v);
Json value(const ContractConfig &v);
Json value(const AdapterIdentity &v);
Json value(const OwnerHarnessConfig &v);
Json value(const BrokerConfig &v);
Json value(const FixtureClockProvenance &v);
Json value(const FixtureStateReceipt &v);
Json value(const ImmutableIngressReceipt &v);
Json value(const AdapterSnapshot &v);
Json value(const SessionSnapshot &v);
Json value(const AdmissionSnapshot &v);
Json value(const SessionIngressVerdict &v);
Json value(const DecodedSessionV1 &v);
Json value(const WireRequestV1 &v);
Json value(const DecodedRequestV1 &v);
Json value(const CanonicalRequest &v);
Json value(const SupervisorEvent &v);
Json value(const DiagnosticIdentity &v);
Json value(const DiagnosticBinding &v);
Json value(const DiagnosticSelectionGrant &v);
Json value(const DiagnosticAdminGrant &v);
Json value(const DiagnosticIngress &v);
Json value(const DiagnosticSnapshot &v);
Json value(const IngressCorrelation &v);
Json value(const AdminCorrelation &v);
Json value(const DiagnosticCorrelation &v);
Json value(const DiagnosticReport &v);
Json value(const DiagnosticOwnerView &v);
Json value(const DiagnosticSelectionCommand &v);
Json value(const DiagnosticAdministrationCommand &v);
Json value(const DiagnosticConsoleResult &v);
Json value(const ReportEvidence &v);
Json value(const AdapterReport &v);
Json value(const DiagObserve &v);
Json value(const DiagPoll &v);
Json value(const DiagAccess &v);
Json value(const DiagEnable &v);
Json value(const DiagDisable &v);
Json value(const DiagEstablish &v);
Json value(const DiagRevoke &v);
Json value(const DiagClear &v);
Json value(const DiagAcknowledge &v);
Json value(const DiagRequest &v);
Json value(const DiagnosticCause &v);
Json value(const DiagnosticTurn &v);
Json value(const DiagnosticSubmission &v);
Json value(const DiagnosticResult &v);
Json value(const AdapterResult &v);

template<typename T>
Json value(const std::optional<T> &v) { return v ? value(*v) : "null"; }

template<typename T, size_t N>
Json value(const T(&items)[N])
{
	std::vector<Json> encoded;
	encoded.reserve(N);

	for (const auto &item : items) { encoded.push_back(value(item)); }

	return array(encoded);
}
} // namespace diagnostic_json
} // namespace research_sitl
