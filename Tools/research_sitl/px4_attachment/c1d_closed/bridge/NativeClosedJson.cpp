#include "NativeClosedJson.hpp"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
namespace research_sitl
{
namespace native_closed_json
{
namespace { bool invalid_json{}; }
void reset() noexcept { invalid_json = false; }
bool valid() noexcept { return !invalid_json; }
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
	if (!std::isfinite(v)) { invalid_json = true; return "null"; }

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
			if (previous->first == current->first) { invalid_json = true; return "null"; }
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

	invalid_json = true; return "null";
}

Json value(ResearchModeState v)
{
	switch (v) {
	case ResearchModeState::NotSelected: return value("NotSelected");

	case ResearchModeState::Selected: return value("Selected");

	case ResearchModeState::Unknown: return value("Unknown");

	}

	invalid_json = true; return "null";
}

Json value(DisarmedPolicy v)
{
	switch (v) {
	case DisarmedPolicy::Reject: return value("Reject");

	case DisarmedPolicy::ValidateOnly: return value("ValidateOnly");

	}

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
}

Json value(SessionGate v)
{
	switch (v) {
	case SessionGate::NoTrustedSession: return value("NoTrustedSession");

	case SessionGate::Established: return value("Established");

	case SessionGate::Revoked: return value("Revoked");

	case SessionGate::EstablishmentRequired: return value("EstablishmentRequired");

	}

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
}

Json value(IngressPacketKind v)
{
	switch (v) {
	case IngressPacketKind::None: return value("None");

	case IngressPacketKind::Acknowledgement: return value("Acknowledgement");

	case IngressPacketKind::Request: return value("Request");

	}

	invalid_json = true; return "null";
}

Json value(DiagnosticDomain v)
{
	switch (v) {
	case DiagnosticDomain::Flight: return value("Flight");

	case DiagnosticDomain::SitlDisarmedDiagnostic: return value("SitlDisarmedDiagnostic");

	}

	invalid_json = true; return "null";
}

Json value(DiagnosticCloseReason v)
{
	switch (v) {
	case DiagnosticCloseReason::OperatorDetach: return value("OperatorDetach");

	case DiagnosticCloseReason::OperatorLost: return value("OperatorLost");

	case DiagnosticCloseReason::OwnerStop: return value("OwnerStop");

	case DiagnosticCloseReason::SourceContinuityLost: return value("SourceContinuityLost");

	case DiagnosticCloseReason::OwnerEvidenceExhausted: return value("OwnerEvidenceExhausted");

	case DiagnosticCloseReason::OwnerConfigurationInvalid: return value("OwnerConfigurationInvalid");

#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)

	case DiagnosticCloseReason::TransportFault: return value("TransportFault");

	case DiagnosticCloseReason::WorkerResourceExhausted: return value("WorkerResourceExhausted");

	case DiagnosticCloseReason::AdapterCapacityExhausted: return value("AdapterCapacityExhausted");
#endif

	}

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
}

Json value(DiagnosticSelectionState v)
{
	switch (v) {
	case DiagnosticSelectionState::Disabled: return value("Disabled");

	case DiagnosticSelectionState::SelectedForValidation: return value("SelectedForValidation");

	case DiagnosticSelectionState::Faulted: return value("Faulted");

	}

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
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

	invalid_json = true; return "null";
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

Json value(const DiagnosticCause &v)
{
	switch (v.index()) {
	case 0: return object({{"type", value("DiagnosticLocalReason")}, {"value", value(std::get<DiagnosticLocalReason>(v))}});

	case 1: return object({{"type", value("DecisionCode")}, {"value", value(std::get<DecisionCode>(v))}});

	case 2: return object({{"type", value("AdminCode")}, {"value", value(std::get<AdminCode>(v))}});

	case 3: return object({{"type", value("IngressCode")}, {"value", value(std::get<IngressCode>(v))}});
	}

	invalid_json = true; return "null";
}

Json value(const DiagnosticResult &v)
{
	if (v.record_count > 4 || (v.has_operation_record ? v.operation_record_index >= v.record_count : v.operation_record_index != 0)) {
		invalid_json = true; return "null";
	}

	std::vector<Json> records;

	for (uint8_t i = 0; i < 4; ++i) { records.push_back(value(v.records[i])); }

	return object({
		{"code", value(v.code)}, {"snapshot", value(v.snapshot)}, {"record_count", value(v.record_count)},
		{"records", array(records)}, {"has_operation_record", value(v.has_operation_record)},
		{"operation_record_index", value(v.operation_record_index)}, {"grant_consumed", value(v.grant_consumed)},
		{"action_performed", value(v.action_performed)}
	});
}

Json value(DiagnosticExecutionOrigin v)
{
	switch (v) {
	case DiagnosticExecutionOrigin::SyntheticOffline: return value("SyntheticOffline");

	case DiagnosticExecutionOrigin::SyntheticNativeContract: return value("SyntheticNativeContract");

	case DiagnosticExecutionOrigin::NativePosixSitl: return value("NativePosixSitl");
	}

	invalid_json = true;
	return "null";
}

Json value(NativeSourceKind v)
{
	switch (v) {
	case NativeSourceKind::Initial: return value("Initial");

	case NativeSourceKind::Transition: return value("Transition");

	case NativeSourceKind::Sample: return value("Sample");

	case NativeSourceKind::Stop: return value("Stop");
	}

	invalid_json = true;
	return "null";
}

Json value(NativeOwnerState v)
{
	switch (v) {
	case NativeOwnerState::AwaitingInitial: return value("AwaitingInitial");

	case NativeOwnerState::Observing: return value("Observing");

	case NativeOwnerState::Stopped: return value("Stopped");

	case NativeOwnerState::Faulted: return value("Faulted");
	}

	invalid_json = true;
	return "null";
}

Json value(NativeOwnerFault v)
{
	switch (v) {
	case NativeOwnerFault::None: return value("None");

	case NativeOwnerFault::InvalidConfiguration: return value("InvalidConfiguration");

	case NativeOwnerFault::MissingInitial: return value("MissingInitial");

	case NativeOwnerFault::RepeatedInitial: return value("RepeatedInitial");

	case NativeOwnerFault::InvalidState: return value("InvalidState");

	case NativeOwnerFault::StateContradiction: return value("StateContradiction");

	case NativeOwnerFault::TimeInvalid: return value("TimeInvalid");

	case NativeOwnerFault::TimeRegressed: return value("TimeRegressed");

	case NativeOwnerFault::CounterExhausted: return value("CounterExhausted");

	case NativeOwnerFault::EvidenceExhausted: return value("EvidenceExhausted");

	case NativeOwnerFault::AdapterTerminal: return value("AdapterTerminal");

	case NativeOwnerFault::AdapterUnavailable: return value("AdapterUnavailable");

	case NativeOwnerFault::SourceLifetimeLost: return value("SourceLifetimeLost");
	}

	invalid_json = true;
	return "null";
}

Json value(const NativeDiagnosticIdentity &v)
{
	return object({{"owner_lifetime_id", value(v.owner_lifetime_id)}, {"native_run_id", value(v.native_run_id)}, {"origin", value(v.origin)}});
}

Json value(const RawState &v)
{
	return object({{"arming_state", value(v.arming_state)}, {"nav_state", value(v.nav_state)}});
}

Json value(const NativeSourceObservation &v)
{
	return object({{"kind", value(v.kind)}, {"before", value(v.before)}, {"after", value(v.after)}, {"source_time_us", value(v.source_time_us)}});
}

Json value(const NativeSourceSnapshot &v)
{
	return object({{"initialized", value(v.initialized)}, {"state", value(v.state)}, {"sequence", value(v.sequence)}, {"transition_epoch", value(v.transition_epoch)}, {"physical_generation", value(v.physical_generation)}, {"committed_time_us", value(v.committed_time_us)}, {"checked_time_us", value(v.checked_time_us)}});
}

Json value(const NativeSourceRecord &v)
{
	return object({{"observation", value(v.observation)}, {"before", value(v.before)}, {"after", value(v.after)}, {"adapter_input", value(v.adapter_input)}, {"result", value(v.result)}});
}

Json value(const NativeClosureRecord &v)
{
	return object({{"fault", value(v.fault)}, {"attempted", value(v.attempted)}, {"retained_source", value(v.retained_source)}, {"adapter_input", value(v.adapter_input)}, {"result", value(v.result)}});
}

Json value(const NativeClosedOwnerConfig &v)
{
	return object({{"contract", value(v.contract)}, {"adapter_identity", value(v.adapter_identity)}, {"identity", value(v.identity)}, {"producer_lifetime_id", value(v.producer_lifetime_id)}, {"clock_epoch", value(v.clock_epoch)}});
}

Json value(const NativeOwnerSummary &v)
{
	return object({{"state", value(v.state)}, {"fault", value(v.fault)}, {"source", value(v.source)}, {"record_count", value(v.record_count)}, {"has_closure", value(v.has_closure)}});
}
}
}
