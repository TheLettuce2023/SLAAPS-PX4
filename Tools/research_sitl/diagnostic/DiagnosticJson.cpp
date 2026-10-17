#include "DiagnosticJson.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace research_sitl
{
namespace diagnostic_json
{
Json value(const std::string &text)
{
	static constexpr char hexadecimal[] = "0123456789abcdef";
	Json result = "\"";

	for (unsigned char character : text) {
		if (character == '\\' || character == '"') {
			result += '\\';
			result += static_cast<char>(character);

		} else if (character < 0x20) {
			result += "\\u00";
			result += hexadecimal[character >> 4];
			result += hexadecimal[character & 0x0f];

		} else {
			result += static_cast<char>(character);
		}
	}

	return result + "\"";
}

Json value(const char *text) { return value(std::string(text)); }
Json value(bool v) { return v ? "true" : "false"; }
Json value(uint8_t v) { return std::to_string(v); }
Json value(uint16_t v) { return std::to_string(v); }
Json value(uint32_t v) { return std::to_string(v); }
Json value(uint64_t v) { return value(std::to_string(v)); }
Json value(int64_t v) { return value(std::to_string(v)); }

Json value(float v)
{
	if (!std::isfinite(v)) { throw std::invalid_argument("Nonfinite JSON float"); }

	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(std::numeric_limits<float>::max_digits10) << v;
	auto encoded = stream.str();

	if (encoded.find_first_of(".eE") == std::string::npos) { encoded += ".0"; }

	return encoded;
}

Json object(std::initializer_list<std::pair<std::string, Json>> members)
{
	Json result = "{";
	bool first = true;

	for (auto current = members.begin(); current != members.end(); ++current) {
		for (auto previous = members.begin(); previous != current; ++previous) {
			if (previous->first == current->first) { throw std::invalid_argument("Duplicate JSON key"); }
		}

		if (!first) { result += ','; }

		first = false;
		result += value(current->first) + ':' + current->second;
	}

	return result + '}';
}

Json array(const std::vector<Json> &items)
{
	Json result = "[";
	bool first = true;

	for (const auto &item : items) {
		if (!first) { result += ','; }

		first = false;
		result += item;
	}

	return result + ']';
}

Json value(const Vec3f &v) { return array({value(v(0)), value(v(1)), value(v(2))}); }

Json value(ArmState v)
{
	switch (v) {
	case ArmState::Disarmed: return value("Disarmed");

	case ArmState::Armed: return value("Armed");

	case ArmState::Unknown: return value("Unknown");

	}

	throw std::invalid_argument("Unknown ArmState");
}

Json value(ResearchModeState v)
{
	switch (v) {
	case ResearchModeState::NotSelected: return value("NotSelected");

	case ResearchModeState::Selected: return value("Selected");

	case ResearchModeState::Unknown: return value("Unknown");

	}

	throw std::invalid_argument("Unknown ResearchModeState");
}

Json value(DisarmedPolicy v)
{
	switch (v) {
	case DisarmedPolicy::Reject: return value("Reject");

	case DisarmedPolicy::ValidateOnly: return value("ValidateOnly");

	}

	throw std::invalid_argument("Unknown DisarmedPolicy");
}

Json value(Frame v)
{
	switch (v) {
	case Frame::Frd: return value("Frd");

	}

	throw std::invalid_argument("Unknown Frame");
}

Json value(Quantity v)
{
	switch (v) {
	case Quantity::InertialAcceleration: return value("InertialAcceleration");

	}

	throw std::invalid_argument("Unknown Quantity");
}

Json value(ReferencePoint v)
{
	switch (v) {
	case ReferencePoint::Cg: return value("Cg");

	}

	throw std::invalid_argument("Unknown ReferencePoint");
}

Json value(DecisionCode v)
{
	switch (v) {
	case DecisionCode::Accepted: return value("Accepted");

	case DecisionCode::ValidatedDisarmed: return value("ValidatedDisarmed");

	case DecisionCode::NotConfigured: return value("NotConfigured");

	case DecisionCode::WrongProtocolVersion: return value("WrongProtocolVersion");

	case DecisionCode::WrongSource: return value("WrongSource");

	case DecisionCode::NoSession: return value("NoSession");

	case DecisionCode::WrongSession: return value("WrongSession");

	case DecisionCode::UnauthorizedSessionEstablishment: return value("UnauthorizedSessionEstablishment");

	case DecisionCode::SessionChangeWhileArmed: return value("SessionChangeWhileArmed");

	case DecisionCode::InvalidTrustedSession: return value("InvalidTrustedSession");

	case DecisionCode::SessionEstablishmentRequired: return value("SessionEstablishmentRequired");

	case DecisionCode::DuplicateSequence: return value("DuplicateSequence");

	case DecisionCode::OutOfOrderSequence: return value("OutOfOrderSequence");

	case DecisionCode::ProducerTimeNotIncreasing: return value("ProducerTimeNotIncreasing");

	case DecisionCode::ClockMappingInvalid: return value("ClockMappingInvalid");

	case DecisionCode::ClockUncertaintyExceeded: return value("ClockUncertaintyExceeded");

	case DecisionCode::ProducerTimeInFuture: return value("ProducerTimeInFuture");

	case DecisionCode::ProducerTimeRegressed: return value("ProducerTimeRegressed");

	case DecisionCode::ValidatorTimeRegressed: return value("ValidatorTimeRegressed");

	case DecisionCode::RequestTooOld: return value("RequestTooOld");

	case DecisionCode::ValidityOutOfRange: return value("ValidityOutOfRange");

	case DecisionCode::RequestExpired: return value("RequestExpired");

	case DecisionCode::TimeArithmeticInvalid: return value("TimeArithmeticInvalid");

	case DecisionCode::RequiredStateInvalid: return value("RequiredStateInvalid");

	case DecisionCode::WrongFrame: return value("WrongFrame");

	case DecisionCode::WrongQuantity: return value("WrongQuantity");

	case DecisionCode::WrongReferencePoint: return value("WrongReferencePoint");

	case DecisionCode::NonFinite: return value("NonFinite");

	case DecisionCode::AxisOutOfBounds: return value("AxisOutOfBounds");

	case DecisionCode::NormOutOfBounds: return value("NormOutOfBounds");

	case DecisionCode::DisarmedRejected: return value("DisarmedRejected");

	case DecisionCode::ModeNotSelected: return value("ModeNotSelected");

	case DecisionCode::ArmStateUnknown: return value("ArmStateUnknown");

	case DecisionCode::ModeStateUnknown: return value("ModeStateUnknown");

	case DecisionCode::Disconnected: return value("Disconnected");

	case DecisionCode::NoEligibleRequest: return value("NoEligibleRequest");

	case DecisionCode::Latched: return value("Latched");

	case DecisionCode::ApiDomainMismatch: return value("ApiDomainMismatch");

	}

	throw std::invalid_argument("Unknown DecisionCode");
}

Json value(SessionGate v)
{
	switch (v) {
	case SessionGate::NoTrustedSession: return value("NoTrustedSession");

	case SessionGate::Established: return value("Established");

	case SessionGate::Revoked: return value("Revoked");

	case SessionGate::EstablishmentRequired: return value("EstablishmentRequired");

	}

	throw std::invalid_argument("Unknown SessionGate");
}

Json value(StatusDecisionOrigin v)
{
	switch (v) {
	case StatusDecisionOrigin::None: return value("None");

	case StatusDecisionOrigin::S1: return value("S1");

	case StatusDecisionOrigin::Local: return value("Local");

	}

	throw std::invalid_argument("Unknown StatusDecisionOrigin");
}

Json value(AdapterCode v)
{
	switch (v) {
	case AdapterCode::Observed: return value("Observed");

	case AdapterCode::NoEligibleRequest: return value("NoEligibleRequest");

	case AdapterCode::GuardRejected: return value("GuardRejected");

	case AdapterCode::TrustedStateFault: return value("TrustedStateFault");

	case AdapterCode::CounterExhausted: return value("CounterExhausted");

	case AdapterCode::NotConfigured: return value("NotConfigured");

	case AdapterCode::EligibleRequest: return value("EligibleRequest");

	case AdapterCode::ApiDomainMismatch: return value("ApiDomainMismatch");

	}

	throw std::invalid_argument("Unknown AdapterCode");
}

Json value(AdapterReportKind v)
{
	switch (v) {
	case AdapterReportKind::Observation: return value("Observation");

	case AdapterReportKind::Poll: return value("Poll");

	case AdapterReportKind::Access: return value("Access");

	case AdapterReportKind::Barrier: return value("Barrier");

	case AdapterReportKind::Terminal: return value("Terminal");

	}

	throw std::invalid_argument("Unknown AdapterReportKind");
}

Json value(SupervisorAction v)
{
	switch (v) {
	case SupervisorAction::Establish: return value("Establish");

	case SupervisorAction::Revoke: return value("Revoke");

	case SupervisorAction::ClearLatchedFault: return value("ClearLatchedFault");

	}

	throw std::invalid_argument("Unknown SupervisorAction");
}

Json value(AdminCode v)
{
	switch (v) {
	case AdminCode::Observed: return value("Observed");

	case AdminCode::Completed: return value("Completed");

	case AdminCode::NotConfigured: return value("NotConfigured");

	case AdminCode::TrustedStateFault: return value("TrustedStateFault");

	case AdminCode::CounterExhausted: return value("CounterExhausted");

	case AdminCode::SupervisorMissing: return value("SupervisorMissing");

	case AdminCode::SupervisorDuplicate: return value("SupervisorDuplicate");

	case AdminCode::SupervisorStreamFault: return value("SupervisorStreamFault");

	case AdminCode::MalformedPacket: return value("MalformedPacket");

	case AdminCode::IngressRejected: return value("IngressRejected");

	case AdminCode::UnsupportedOperation: return value("UnsupportedOperation");

	case AdminCode::PacketOperationReplay: return value("PacketOperationReplay");

	case AdminCode::BindingMismatch: return value("BindingMismatch");

	case AdminCode::EstablishmentContextRejected: return value("EstablishmentContextRejected");

	case AdminCode::ContextRejected: return value("ContextRejected");

	case AdminCode::NoEstablishedSession: return value("NoEstablishedSession");

	case AdminCode::ClearNotLatched: return value("ClearNotLatched");

	case AdminCode::S1ActionRejected: return value("S1ActionRejected");

	case AdminCode::ApiDomainMismatch: return value("ApiDomainMismatch");

	}

	throw std::invalid_argument("Unknown AdminCode");
}

Json value(IngressCode v)
{
	switch (v) {
	case IngressCode::Observed: return value("Observed");

	case IngressCode::Completed: return value("Completed");

	case IngressCode::NotConfigured: return value("NotConfigured");

	case IngressCode::TrustedStateFault: return value("TrustedStateFault");

	case IngressCode::CounterExhausted: return value("CounterExhausted");

	case IngressCode::PrestateRejected: return value("PrestateRejected");

	case IngressCode::MalformedPacket: return value("MalformedPacket");

	case IngressCode::IngressRejected: return value("IngressRejected");

	case IngressCode::UnsupportedSessionOperation: return value("UnsupportedSessionOperation");

	case IngressCode::WireOperationReplay: return value("WireOperationReplay");

	case IngressCode::SessionGateRejected: return value("SessionGateRejected");

	case IngressCode::BindingMismatch: return value("BindingMismatch");

	case IngressCode::AcknowledgementRequired: return value("AcknowledgementRequired");

	case IngressCode::AcknowledgementFrozen: return value("AcknowledgementFrozen");

	case IngressCode::AcknowledgementStatusMismatch: return value("AcknowledgementStatusMismatch");

	case IngressCode::AcknowledgementOrdering: return value("AcknowledgementOrdering");

	case IngressCode::AcknowledgementTimingInvalid: return value("AcknowledgementTimingInvalid");

	case IngressCode::MapInvalid: return value("MapInvalid");

	case IngressCode::MapIntervalInvalid: return value("MapIntervalInvalid");

	case IngressCode::MapArithmeticInvalid: return value("MapArithmeticInvalid");

	case IngressCode::WireVersionInvalid: return value("WireVersionInvalid");

	case IngressCode::WireEnumInvalid: return value("WireEnumInvalid");

	case IngressCode::WireTimeInvalid: return value("WireTimeInvalid");

	case IngressCode::WireNonFinite: return value("WireNonFinite");

	case IngressCode::S1Rejected: return value("S1Rejected");

	case IngressCode::ApiDomainMismatch: return value("ApiDomainMismatch");

	}

	throw std::invalid_argument("Unknown IngressCode");
}

Json value(IngressPacketKind v)
{
	switch (v) {
	case IngressPacketKind::None: return value("None");

	case IngressPacketKind::Acknowledgement: return value("Acknowledgement");

	case IngressPacketKind::Request: return value("Request");

	}

	throw std::invalid_argument("Unknown IngressPacketKind");
}

Json value(DiagnosticDomain v)
{
	switch (v) {
	case DiagnosticDomain::Flight: return value("Flight");

	case DiagnosticDomain::SitlDisarmedDiagnostic: return value("SitlDisarmedDiagnostic");

	}

	throw std::invalid_argument("Unknown DiagnosticDomain");
}

Json value(DiagnosticAction v)
{
	switch (v) {
	case DiagnosticAction::Enable: return value("Enable");

	case DiagnosticAction::Disable: return value("Disable");

	}

	throw std::invalid_argument("Unknown DiagnosticAction");
}

Json value(DiagnosticCloseReason v)
{
	switch (v) {
	case DiagnosticCloseReason::OperatorDetach: return value("OperatorDetach");

	case DiagnosticCloseReason::OperatorLost: return value("OperatorLost");

	}

	throw std::invalid_argument("Unknown DiagnosticCloseReason");
}

Json value(DiagnosticReportKind v)
{
	switch (v) {
	case DiagnosticReportKind::PhysicalBarrier: return value("PhysicalBarrier");

	case DiagnosticReportKind::SelectorBarrier: return value("SelectorBarrier");

	case DiagnosticReportKind::SessionBarrier: return value("SessionBarrier");

	case DiagnosticReportKind::Operation: return value("Operation");

	case DiagnosticReportKind::Terminal: return value("Terminal");

	}

	throw std::invalid_argument("Unknown DiagnosticReportKind");
}

Json value(DiagnosticOperationKind v)
{
	switch (v) {
	case DiagnosticOperationKind::None: return value("None");

	case DiagnosticOperationKind::Observe: return value("Observe");

	case DiagnosticOperationKind::Poll: return value("Poll");

	case DiagnosticOperationKind::Access: return value("Access");

	case DiagnosticOperationKind::Enable: return value("Enable");

	case DiagnosticOperationKind::Disable: return value("Disable");

	case DiagnosticOperationKind::Establish: return value("Establish");

	case DiagnosticOperationKind::Revoke: return value("Revoke");

	case DiagnosticOperationKind::Clear: return value("Clear");

	case DiagnosticOperationKind::Acknowledge: return value("Acknowledge");

	case DiagnosticOperationKind::Request: return value("Request");

	case DiagnosticOperationKind::LifecycleClose: return value("LifecycleClose");

	}

	throw std::invalid_argument("Unknown DiagnosticOperationKind");
}

Json value(DiagnosticSelectionState v)
{
	switch (v) {
	case DiagnosticSelectionState::Disabled: return value("Disabled");

	case DiagnosticSelectionState::SelectedForValidation: return value("SelectedForValidation");

	case DiagnosticSelectionState::Faulted: return value("Faulted");

	}

	throw std::invalid_argument("Unknown DiagnosticSelectionState");
}

Json value(DiagnosticCode v)
{
	switch (v) {
	case DiagnosticCode::Observed: return value("Observed");

	case DiagnosticCode::NoEligibleRequest: return value("NoEligibleRequest");

	case DiagnosticCode::SelectionEnabled: return value("SelectionEnabled");

	case DiagnosticCode::SelectionDisabled: return value("SelectionDisabled");

	case DiagnosticCode::AlreadyDisabled: return value("AlreadyDisabled");

	case DiagnosticCode::RequestValidatedDisarmed: return value("RequestValidatedDisarmed");

	case DiagnosticCode::Rejected: return value("Rejected");

	case DiagnosticCode::Terminal: return value("Terminal");

	case DiagnosticCode::NotConfigured: return value("NotConfigured");

	case DiagnosticCode::WrongDomain: return value("WrongDomain");

	case DiagnosticCode::SessionEstablished: return value("SessionEstablished");

	case DiagnosticCode::SessionRevoked: return value("SessionRevoked");

	case DiagnosticCode::LatchCleared: return value("LatchCleared");

	case DiagnosticCode::Acknowledged: return value("Acknowledged");

	}

	throw std::invalid_argument("Unknown DiagnosticCode");
}

Json value(DiagnosticLocalReason v)
{
	switch (v) {
	case DiagnosticLocalReason::None: return value("None");

	case DiagnosticLocalReason::NotDisarmed: return value("NotDisarmed");

	case DiagnosticLocalReason::FlightModeConflict: return value("FlightModeConflict");

	case DiagnosticLocalReason::SelectorInvalid: return value("SelectorInvalid");

	case DiagnosticLocalReason::SelectorFaulted: return value("SelectorFaulted");

	case DiagnosticLocalReason::SelectionRequired: return value("SelectionRequired");

	case DiagnosticLocalReason::BindingMismatch: return value("BindingMismatch");

	case DiagnosticLocalReason::ContextChanged: return value("ContextChanged");

	case DiagnosticLocalReason::GrantInvalid: return value("GrantInvalid");

	case DiagnosticLocalReason::GrantReplay: return value("GrantReplay");

	case DiagnosticLocalReason::CounterExhausted: return value("CounterExhausted");

	case DiagnosticLocalReason::TrustedStateFault: return value("TrustedStateFault");

	}

	throw std::invalid_argument("Unknown DiagnosticLocalReason");
}

Json value(Intent v)
{
	switch (v) {
	case Intent::Establish: return value("Establish");

	case Intent::Revoke: return value("Revoke");

	case Intent::ClearLatch: return value("ClearLatch");

	}

	throw std::invalid_argument("Unknown Intent");
}

Json value(ConsoleDisposition v)
{
	switch (v) {
	case ConsoleDisposition::NotSubmitted: return value("NotSubmitted");

	case ConsoleDisposition::Submitted: return value("Submitted");

	case ConsoleDisposition::Quarantined: return value("Quarantined");

	}

	throw std::invalid_argument("Unknown ConsoleDisposition");
}

Json value(OwnerOutcome v)
{
	switch (v) {
	case OwnerOutcome::NotSubmitted: return value("NotSubmitted");

	case OwnerOutcome::Completed: return value("Completed");

	case OwnerOutcome::Unknown: return value("Unknown");

	}

	throw std::invalid_argument("Unknown OwnerOutcome");
}

Json value(BrokerCode v)
{
	switch (v) {
	case BrokerCode::Ready: return value("Ready");

	case BrokerCode::Submitted: return value("Submitted");

	case BrokerCode::NotConfigured: return value("NotConfigured");

	case BrokerCode::NotAttached: return value("NotAttached");

	case BrokerCode::Busy: return value("Busy");

	case BrokerCode::InvalidIntent: return value("InvalidIntent");

	case BrokerCode::InvalidTicket: return value("InvalidTicket");

	case BrokerCode::OwnerUnavailable: return value("OwnerUnavailable");

	case BrokerCode::OwnerNotReady: return value("OwnerNotReady");

	case BrokerCode::ContextChanged: return value("ContextChanged");

	case BrokerCode::Expired: return value("Expired");

	case BrokerCode::CounterExhausted: return value("CounterExhausted");

	case BrokerCode::AuthorityDiscontinuity: return value("AuthorityDiscontinuity");

	case BrokerCode::OwnerDeclined: return value("OwnerDeclined");

	case BrokerCode::OutcomeUnknown: return value("OutcomeUnknown");

	case BrokerCode::AdapterUnconsumed: return value("AdapterUnconsumed");

	case BrokerCode::InvalidOwnerResult: return value("InvalidOwnerResult");

	case BrokerCode::Quarantined: return value("Quarantined");

	}

	throw std::invalid_argument("Unknown BrokerCode");
}

Json value(const TrustedSession &v)
{
	return object({
		{"owner", value(v.owner)},
		{"session", value(v.session)}
	});
}

Json value(const TrustedClockMap &v)
{
	return object({
		{"valid", value(v.valid)},
		{"source", value(v.source)},
		{"session", value(v.session)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"map_epoch", value(v.map_epoch)},
		{"validator_minus_producer_us", value(v.validator_minus_producer_us)},
		{"uncertainty_us", value(v.uncertainty_us)},
		{"valid_from_producer_us", value(v.valid_from_producer_us)},
		{"valid_until_producer_us", value(v.valid_until_producer_us)}
	});
}

Json value(const EvaluationContext &v)
{
	return object({
		{"validator_receive_time_us", value(v.validator_receive_time_us)},
		{"arm_state", value(v.arm_state)},
		{"mode_state", value(v.mode_state)},
		{"clock_mapping_valid", value(v.clock_mapping_valid)},
		{"transport_connected", value(v.transport_connected)},
		{"required_state_valid", value(v.required_state_valid)}
	});
}

Json value(const TrustedStateSample &v)
{
	return object({
		{"generation", value(v.generation)},
		{"observer_continuous", value(v.observer_continuous)},
		{"request_guard_context", value(v.request_guard_context)},
		{"map", value(v.map)}
	});
}

Json value(const Decision &v)
{
	return object({
		{"code", value(v.code)},
		{"syntactically_valid", value(v.syntactically_valid)},
		{"eligible_for_control", value(v.eligible_for_control)},
		{"latched", value(v.latched)},
		{"first_latched_code", value(v.first_latched_code)},
		{"conservative_age_valid", value(v.conservative_age_valid)},
		{"conservative_age_us", value(v.conservative_age_us)},
		{"rejection_count", value(v.rejection_count)}
	});
}

Json value(const AccelerationBounds &v)
{
	return object({
		{"minimum_m_s2", value(v.minimum_m_s2)},
		{"maximum_m_s2", value(v.maximum_m_s2)},
		{"maximum_norm_m_s2", value(v.maximum_norm_m_s2)}
	});
}

Json value(const TimingRequirements &v)
{
	return object({
		{"minimum_validity_us", value(v.minimum_validity_us)},
		{"maximum_validity_us", value(v.maximum_validity_us)},
		{"maximum_command_age_us", value(v.maximum_command_age_us)},
		{"maximum_future_skew_us", value(v.maximum_future_skew_us)},
		{"maximum_clock_uncertainty_us", value(v.maximum_clock_uncertainty_us)}
	});
}

Json value(const ContractConfig &v)
{
	return object({
		{"protocol_version", value(v.protocol_version)},
		{"permitted_source", value(v.permitted_source)},
		{"disarmed_policy", value(v.disarmed_policy)},
		{"acceleration_bounds", value(v.acceleration_bounds)},
		{"timing", value(v.timing)},
		{"quaternion_norm_tolerance", value(v.quaternion_norm_tolerance)}
	});
}

Json value(const AdapterIdentity &v)
{
	return object({
		{"controller_instance_id", value(v.controller_instance_id)}
	});
}

Json value(const OwnerHarnessConfig &v)
{
	return object({
		{"adapter_contract", value(v.adapter_contract)},
		{"adapter_identity", value(v.adapter_identity)},
		{"fixture_run_id", value(v.fixture_run_id)},
		{"maximum_receipt_age_us", value(v.maximum_receipt_age_us)}
	});
}

Json value(const BrokerConfig &v)
{
	return object({
		{"maximum_confirmation_age_us", value(v.maximum_confirmation_age_us)}
	});
}

Json value(const FixtureClockProvenance &v)
{
	return object({
		{"fixture_run_id", value(v.fixture_run_id)},
		{"observation_id", value(v.observation_id)},
		{"validator_time_us", value(v.validator_time_us)},
		{"continuous", value(v.continuous)}
	});
}

Json value(const FixtureStateReceipt &v)
{
	return object({
		{"sample", value(v.sample)},
		{"clock", value(v.clock)},
		{"fixture_state_stream_id", value(v.fixture_state_stream_id)}
	});
}

Json value(const ImmutableIngressReceipt &v)
{
	return object({
		{"fixture_run_id", value(v.fixture_run_id)},
		{"receipt_id", value(v.receipt_id)},
		{"received_validator_time_us", value(v.received_validator_time_us)},
		{"packet", value(v.packet)},
		{"verdict", value(v.verdict)}
	});
}

Json value(const AdapterSnapshot &v)
{
	return object({
		{"initialized", value(v.initialized)},
		{"terminal", value(v.terminal)},
		{"state_generation", value(v.state_generation)},
		{"transition_epoch", value(v.transition_epoch)},
		{"barrier_report_sequence", value(v.barrier_report_sequence)},
		{"barrier_validator_time_us", value(v.barrier_validator_time_us)},
		{"last_report_sequence", value(v.last_report_sequence)}
	});
}

Json value(const SessionSnapshot &v)
{
	return object({
		{"gate", value(v.gate)},
		{"has_last_successful_session", value(v.has_last_successful_session)},
		{"last_successful_session", value(v.last_successful_session)},
		{"has_wire_operation_floor", value(v.has_wire_operation_floor)},
		{"wire_operation_floor", value(v.wire_operation_floor)},
		{"has_supervisor_floor", value(v.has_supervisor_floor)},
		{"supervisor_floor", value(v.supervisor_floor)}
	});
}

Json value(const AdmissionSnapshot &v)
{
	return object({
		{"acknowledgement_current", value(v.acknowledgement_current)},
		{"acknowledgement_operation_sequence", value(v.acknowledgement_operation_sequence)},
		{"acknowledged_status_sequence", value(v.acknowledged_status_sequence)},
		{"acknowledgement_raw_producer_time_us", value(v.acknowledgement_raw_producer_time_us)},
		{"mapped_acknowledgement_time_us", value(v.mapped_acknowledgement_time_us)},
		{"earliest_acknowledgement_time_us", value(v.earliest_acknowledgement_time_us)},
		{"latest_acknowledgement_time_us", value(v.latest_acknowledgement_time_us)},
		{"source", value(v.source)},
		{"session", value(v.session)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"transition_epoch", value(v.transition_epoch)},
		{"map_epoch", value(v.map_epoch)},
		{"map", value(v.map)}
	});
}

Json value(const SessionIngressVerdict &v)
{
	return object({
		{"mavlink2", value(v.mavlink2)},
		{"parser_authenticated", value(v.parser_authenticated)},
		{"configured_header_channel_source_binding", value(v.configured_header_channel_source_binding)},
		{"configured_target_binding", value(v.configured_target_binding)}
	});
}

Json value(const DecodedSessionV1 &v)
{
	return object({
		{"controller_instance_id", value(v.controller_instance_id)},
		{"transition_epoch", value(v.transition_epoch)},
		{"clock_mapping_epoch", value(v.clock_mapping_epoch)},
		{"source", value(v.source)},
		{"session", value(v.session)},
		{"operation_sequence", value(v.operation_sequence)},
		{"producer_acknowledgement_monotonic_us", value(v.producer_acknowledgement_monotonic_us)},
		{"acknowledged_status_sequence", value(v.acknowledged_status_sequence)},
		{"protocol_version", value(v.protocol_version)},
		{"target_system", value(v.target_system)},
		{"target_component", value(v.target_component)},
		{"operation", value(v.operation)},
		{"flags", value(v.flags)},
		{"reserved", value(v.reserved)}
	});
}

Json value(const WireRequestV1 &v)
{
	return object({
		{"source_id", value(v.source_id)},
		{"session_id", value(v.session_id)},
		{"sequence", value(v.sequence)},
		{"producer_monotonic_us", value(v.producer_monotonic_us)},
		{"producer_expiry_monotonic_us", value(v.producer_expiry_monotonic_us)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"transition_epoch", value(v.transition_epoch)},
		{"clock_mapping_epoch", value(v.clock_mapping_epoch)},
		{"acceleration_x_frd_m_s2", value(v.acceleration_x_frd_m_s2)},
		{"acceleration_y_frd_m_s2", value(v.acceleration_y_frd_m_s2)},
		{"acceleration_z_frd_m_s2", value(v.acceleration_z_frd_m_s2)},
		{"protocol_version", value(v.protocol_version)},
		{"frame", value(v.frame)},
		{"quantity", value(v.quantity)},
		{"reference_point", value(v.reference_point)}
	});
}

Json value(const DecodedRequestV1 &v)
{
	return object({
		{"wire", value(v.wire)},
		{"target_system", value(v.target_system)},
		{"target_component", value(v.target_component)},
		{"flags", value(v.flags)},
		{"reserved", value(v.reserved)}
	});
}

Json value(const CanonicalRequest &v)
{
	return object({
		{"protocol_version", value(v.protocol_version)},
		{"source", value(v.source)},
		{"session", value(v.session)},
		{"sequence", value(v.sequence)},
		{"producer_monotonic_us", value(v.producer_monotonic_us)},
		{"producer_time_in_validator_domain_us", value(v.producer_time_in_validator_domain_us)},
		{"producer_expiry_monotonic_us", value(v.producer_expiry_monotonic_us)},
		{"expiry_time_in_validator_domain_us", value(v.expiry_time_in_validator_domain_us)},
		{"clock_mapping_uncertainty_us", value(v.clock_mapping_uncertainty_us)},
		{"frame", value(v.frame)},
		{"quantity", value(v.quantity)},
		{"reference_point", value(v.reference_point)},
		{"acceleration_frd_m_s2", value(v.acceleration_frd_m_s2)}
	});
}

Json value(const SupervisorEvent &v)
{
	return object({
		{"event_id", value(v.event_id)},
		{"expected_state_generation", value(v.expected_state_generation)},
		{"expected_transition_epoch", value(v.expected_transition_epoch)},
		{"expected_controller_instance_id", value(v.expected_controller_instance_id)},
		{"action", value(v.action)},
		{"has_session_binding", value(v.has_session_binding)},
		{"session", value(v.session)}
	});
}

Json value(const DiagnosticIdentity &v)
{
	return object({
		{"owner_lifetime_id", value(v.owner_lifetime_id)},
		{"fixture_run_id", value(v.fixture_run_id)}
	});
}

Json value(const DiagnosticBinding &v)
{
	return object({
		{"owner_lifetime_id", value(v.owner_lifetime_id)},
		{"fixture_run_id", value(v.fixture_run_id)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"operator_instance", value(v.operator_instance)},
		{"state_generation", value(v.state_generation)},
		{"transition_epoch", value(v.transition_epoch)},
		{"selector_epoch", value(v.selector_epoch)},
		{"prospective_map", value(v.prospective_map)}
	});
}

Json value(const DiagnosticSelectionGrant &v)
{
	return object({
		{"event_id", value(v.event_id)},
		{"action", value(v.action)},
		{"expected", value(v.expected)}
	});
}

Json value(const DiagnosticAdminGrant &v)
{
	return object({
		{"expected", value(v.expected)},
		{"event", value(v.event)}
	});
}

Json value(const DiagnosticIngress &v)
{
	return object({
		{"owner_lifetime_id", value(v.owner_lifetime_id)},
		{"fixture_run_id", value(v.fixture_run_id)},
		{"authentication", value(v.authentication)}
	});
}

Json value(const DiagnosticSnapshot &v)
{
	return object({
		{"domain", value(v.domain)},
		{"identity", value(v.identity)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"configured", value(v.configured)},
		{"initialized", value(v.initialized)},
		{"terminal", value(v.terminal)},
		{"selection", value(v.selection)},
		{"selector_epoch", value(v.selector_epoch)},
		{"selection_reason", value(v.selection_reason)},
		{"operator_attached", value(v.operator_attached)},
		{"operator_instance", value(v.operator_instance)},
		{"adapter", value(v.adapter)},
		{"session", value(v.session)},
		{"admission", value(v.admission)},
		{"latched", value(v.latched)},
		{"first_latched_code", value(v.first_latched_code)},
		{"rejection_count", value(v.rejection_count)},
		{"has_last_accepted_sequence", value(v.has_last_accepted_sequence)},
		{"last_accepted_sequence", value(v.last_accepted_sequence)},
		{"last_accepted_authenticated", value(v.last_accepted_authenticated)}
	});
}

Json value(const IngressCorrelation &v)
{
	return object({
		{"packet_kind", value(v.packet_kind)},
		{"source", value(v.source)},
		{"session", value(v.session)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"transition_epoch", value(v.transition_epoch)},
		{"map_epoch", value(v.map_epoch)},
		{"packet_sequence", value(v.packet_sequence)},
		{"acknowledged_status_sequence", value(v.acknowledged_status_sequence)}
	});
}

Json value(const AdminCorrelation &v)
{
	return object({
		{"has_packet", value(v.has_packet)},
		{"packet_source", value(v.packet_source)},
		{"packet_session", value(v.packet_session)},
		{"packet_operation_sequence", value(v.packet_operation_sequence)},
		{"has_supervisor_event", value(v.has_supervisor_event)},
		{"supervisor_event_id", value(v.supervisor_event_id)},
		{"supervisor_event_consumed", value(v.supervisor_event_consumed)}
	});
}

Json value(const DiagnosticCorrelation &v)
{
	return object({
		{"operation", value(v.operation)},
		{"ingress", value(v.ingress)},
		{"admin", value(v.admin)},
		{"operator_instance", value(v.operator_instance)},
		{"close_reason", value(v.close_reason)},
		{"authentication", value(v.authentication)}
	});
}

Json value(const DiagnosticReport &v)
{
	return object({
		{"domain", value(v.domain)},
		{"identity", value(v.identity)},
		{"report_sequence", value(v.report_sequence)},
		{"transition_epoch", value(v.transition_epoch)},
		{"selector_epoch", value(v.selector_epoch)},
		{"kind", value(v.kind)},
		{"code", value(v.code)},
		{"cause", value(v.cause)},
		{"correlation", value(v.correlation)},
		{"state_reference_valid", value(v.state_reference_valid)},
		{"physical", value(v.physical)},
		{"snapshot", value(v.snapshot)},
		{"common_decision", value(v.common_decision)},
		{"eligible_for_control", value(v.eligible_for_control)}
	});
}

Json value(const DiagnosticOwnerView &v)
{
	return object({
		{"binding", value(v.binding)},
		{"validator_time_us", value(v.validator_time_us)},
		{"state", value(v.state)},
		{"physical", value(v.physical)},
		{"can_enable", value(v.can_enable)},
		{"can_disable", value(v.can_disable)},
		{"can_admin", value(v.can_admin)}
	});
}

Json value(const DiagnosticSelectionCommand &v)
{
	return object({
		{"action", value(v.action)},
		{"prospective", value(v.prospective)},
		{"event_id", value(v.event_id)}
	});
}

Json value(const DiagnosticAdministrationCommand &v)
{
	return object({
		{"intent", value(v.intent)},
		{"session", value(v.session)},
		{"event_id", value(v.event_id)}
	});
}

Json value(const DiagnosticConsoleResult &v)
{
	return object({
		{"disposition", value(v.disposition)},
		{"broker_code", value(v.broker_code)},
		{"diagnostic_code", value(v.diagnostic_code)},
		{"ticket", value(v.ticket)},
		{"event_consumed", value(v.event_consumed)},
		{"action_performed", value(v.action_performed)}
	});
}

Json value(const ReportEvidence &v)
{
	return object({
		{"captured", value(v.captured)},
		{"controller_instance_id", value(v.controller_instance_id)},
		{"state_reference_valid", value(v.state_reference_valid)},
		{"clock_mapping_epoch", value(v.clock_mapping_epoch)},
		{"arm_state", value(v.arm_state)},
		{"mode_state", value(v.mode_state)},
		{"session_gate", value(v.session_gate)},
		{"transport_connected", value(v.transport_connected)},
		{"clock_mapping_valid", value(v.clock_mapping_valid)},
		{"trusted_source", value(v.trusted_source)},
		{"trusted_session", value(v.trusted_session)},
		{"packet_mavlink2", value(v.packet_mavlink2)},
		{"packet_parser_authenticated", value(v.packet_parser_authenticated)},
		{"packet_header_source_bound", value(v.packet_header_source_bound)},
		{"packet_target_bound", value(v.packet_target_bound)},
		{"acknowledgement_current", value(v.acknowledgement_current)},
		{"has_last_accepted_sequence", value(v.has_last_accepted_sequence)},
		{"last_accepted_sequence", value(v.last_accepted_sequence)},
		{"last_accepted_authenticated", value(v.last_accepted_authenticated)},
		{"latched", value(v.latched)},
		{"first_latched_code", value(v.first_latched_code)},
		{"rejection_count", value(v.rejection_count)},
		{"syntactically_valid", value(v.syntactically_valid)},
		{"conservative_age_valid", value(v.conservative_age_valid)},
		{"conservative_age_us", value(v.conservative_age_us)},
		{"decision_origin", value(v.decision_origin)},
		{"local_wire_decision_code", value(v.local_wire_decision_code)}
	});
}

Json value(const AdapterReport &v)
{
	return object({
		{"report_sequence", value(v.report_sequence)},
		{"transition_epoch", value(v.transition_epoch)},
		{"validator_time_us", value(v.validator_time_us)},
		{"kind", value(v.kind)},
		{"code", value(v.code)},
		{"has_s1_decision", value(v.has_s1_decision)},
		{"s1_decision", value(v.s1_decision)},
		{"evidence", value(v.evidence)}
	});
}

Json value(const DiagObserve &)
{
	return object({{"kind", value("DiagObserve")}, {"value", object({})}});
}

Json value(const DiagPoll &)
{
	return object({{"kind", value("DiagPoll")}, {"value", object({})}});
}

Json value(const DiagAccess &)
{
	return object({{"kind", value("DiagAccess")}, {"value", object({})}});
}

Json value(const DiagEnable &v)
{
	return object({{"kind", value("DiagEnable")}, {"value", object({
				{"grant", value(v.grant)}
			})
		}});
}

Json value(const DiagDisable &v)
{
	return object({{"kind", value("DiagDisable")}, {"value", object({
				{"grant", value(v.grant)}
			})
		}});
}

Json value(const DiagEstablish &v)
{
	return object({{"kind", value("DiagEstablish")}, {"value", object({
				{"packet", value(v.packet)},
				{"ingress", value(v.ingress)},
				{"grant", value(v.grant)}
			})
		}});
}

Json value(const DiagRevoke &v)
{
	return object({{"kind", value("DiagRevoke")}, {"value", object({
				{"packet", value(v.packet)},
				{"ingress", value(v.ingress)},
				{"grant", value(v.grant)}
			})
		}});
}

Json value(const DiagClear &v)
{
	return object({{"kind", value("DiagClear")}, {"value", object({
				{"grant", value(v.grant)}
			})
		}});
}

Json value(const DiagAcknowledge &v)
{
	return object({{"kind", value("DiagAcknowledge")}, {"value", object({
				{"packet", value(v.packet)},
				{"ingress", value(v.ingress)}
			})
		}});
}

Json value(const DiagRequest &v)
{
	return object({{"kind", value("DiagRequest")}, {"value", object({
				{"packet", value(v.packet)},
				{"ingress", value(v.ingress)}
			})
		}});
}

Json value(const DiagnosticCause &v)
{
	switch (v.index()) {
	case 0: return object({{"type", value("DiagnosticLocalReason")}, {"value", value(std::get<DiagnosticLocalReason>(v))}});

	case 1: return object({{"type", value("DecisionCode")}, {"value", value(std::get<DecisionCode>(v))}});

	case 2: return object({{"type", value("AdminCode")}, {"value", value(std::get<AdminCode>(v))}});

	case 3: return object({{"type", value("IngressCode")}, {"value", value(std::get<IngressCode>(v))}});
	}

	throw std::invalid_argument("Invalid diagnostic cause");
}

Json value(const DiagnosticTurn &v)
{
	return std::visit([](const auto & operation) { return value(operation); }, v);
}

Json value(const DiagnosticSubmission &v)
{
	Json command;

	if (const auto *selection = std::get_if<DiagnosticSelectionCommand>(&v.command)) {
		command = object({{"kind", value("DiagnosticSelectionCommand")}, {"value", value(*selection)}});

	} else {
		command = object({{"kind", value("DiagnosticAdministrationCommand")},
			{"value", value(std::get<DiagnosticAdministrationCommand>(v.command))}});
	}

	return object({{"expected_view", value(v.expected_view)}, {"command", command}});
}

Json value(const DiagnosticResult &v)
{
	if (v.record_count > 4 || (v.has_operation_record ? v.operation_record_index >= v.record_count : v.operation_record_index != 0)) {
		throw std::invalid_argument("Invalid diagnostic record bounds");
	}

	std::vector<Json> records;

	for (uint8_t i = 0; i < v.record_count; ++i) { records.push_back(value(v.records[i])); }

	return object({
		{"code", value(v.code)}, {"snapshot", value(v.snapshot)}, {"record_count", value(v.record_count)},
		{"records", array(records)}, {"has_operation_record", value(v.has_operation_record)},
		{"operation_record_index", value(v.operation_record_index)}, {"grant_consumed", value(v.grant_consumed)},
		{"action_performed", value(v.action_performed)}
	});
}

Json value(const AdapterResult &v)
{
	if (v.report_count > 2) { throw std::invalid_argument("Invalid ordinary record bounds"); }

	return object({
		{"code", value(v.code)}, {"admission_closed", value(v.admission_closed)}, {"snapshot", value(v.snapshot)},
		{"report_count", value(v.report_count)}, {"reports", value(v.reports)}
	});
}
} // namespace diagnostic_json
} // namespace research_sitl
