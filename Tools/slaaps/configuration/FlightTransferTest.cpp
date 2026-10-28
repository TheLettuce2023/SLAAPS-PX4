#include <lib/slaaps/ManualWorker.hpp>
#include <lib/slaaps/ManualOutputBank.hpp>
#include <lib/slaaps/ManualPermissionTransport.hpp>
#include <lib/slaaps/ManualTransport.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
using namespace slaaps;
using namespace research_stabilized_manual;
constexpr uint64_t kStart = 1000000;
constexpr uint64_t kEdge = kStart + 13000;

// Extend the existing explicitly synthetic runner profile. These data describe
// a test force along X and the existing coupled canard moments, not a vehicle.
VehicleConfiguration configuration(const VehicleConfiguration &base)
{
	auto c = base;
	// Explicit synthetic solver conditioning for this 20 N span and residual
	// weights; retain the allocator's mixed-unit regularization floor.
	c.regularization = .0001F;
	c.edf_rise = c.edf_fall = 100.F;
	c.wrench.enabled = c.acceleration_control.enabled = c.feedback.enabled = 1;
	c.wrench.edf_force_per_thrust[0] = 1.F;
	c.wrench.trim_force[0] = c.condition_thrust;
	c.feedback.maximum_age_us = c.feedback.maximum_bias_age_us = 100000;
	c.feedback.latency_bound_us = c.feedback.settling_us = 1000;
	c.feedback.maximum_specific_force_m_s2 = c.feedback.maximum_acceleration_m_s2 = 100.F;
	c.feedback.gravity_m_s2 = 9.F;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		c.acceleration_control.acceleration_feedback_p[axis] = 1.F;
		c.acceleration_control.correction_limit[axis] = .25F;
		c.wrench.force_residual_tolerance[axis] = c.wrench.moment_residual_tolerance[axis] = .05F;
	}

	auto &s = c.schedule;
	s.enabled = 1;
	s.airspeed_instance = s.air_data_instance = s.battery_instance = 0;
	s.battery_id = 1;
	s.maximum_age_us = 100000;
	s.indicated_airspeed_m_s[0] = 0.F;
	s.indicated_airspeed_m_s[1] = 20.F;
	s.edf_command[0] = 0.F;
	s.edf_command[1] = 1.F;
	s.density_min = 1.F; s.density_max = 2.F;
	s.voltage_min = 20.F; s.voltage_max = 26.F;
	s.filter_time_s = .05F;
	s.airspeed_filter_rate = s.airspeed_transient_rate = 1000.F;
	s.edf_filter_rate = s.edf_transient_rate = 100.F;

	for (unsigned i = 0; i < 2; ++i) {
		for (unsigned j = 0; j < 2; ++j) {
			auto &corner = s.corner[i][j];
			corner.edf_force_per_thrust[0] = 1.F;
			corner.trim_force[0] = j ? c.edf_thrust[kThrustPoints - 1] : c.edf_thrust[0];

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.rate_p[axis] = c.axis[axis].rate_p;
				corner.rate_i[axis] = c.axis[axis].rate_i;
				corner.rate_d[axis] = c.axis[axis].rate_d;

				for (unsigned canard = 0; canard < kCanards; ++canard) {
					corner.moment_derivative[axis][canard] = c.canard[canard].moment_derivative[axis];
				}
			}
		}
	}

	auto &p = c.companion;
	p.enabled = p.admission_enabled = 1;
	p.mavlink_instance = 0; p.source_system = 42; p.source_component = 191;
	p.source_id = 1; p.maximum_age_us = 100000; p.disarmed_policy = 2;

	for (unsigned axis = 0; axis < kAxes; ++axis) { p.acceleration_minimum[axis] = -100.F; p.acceleration_maximum[axis] = 100.F; }

	p.acceleration_norm_max = 100.F;
	p.minimum_validity_us = 1000; p.maximum_validity_us = p.maximum_command_age_us = 200000;
	p.maximum_future_skew_us = 1000; p.maximum_clock_uncertainty_us = 10000;
	p.maximum_relative_drift_ppm = 50; p.timestamp_uncertainty_us = 1000;
	p.maximum_round_trip_us = 10000; p.map_interval_us = 500000;
	return c;
}

void condition(OperatingConditionSample &sample, uint64_t now)
{
	sample.observed_us = sample.airspeed_timestamp_us = sample.airspeed_sample_us = now;
	sample.air_data_timestamp_us = sample.air_data_sample_us = sample.battery_timestamp_us = now;
	sample.deadline_us = now + 100001;
	sample.indicated_airspeed_m_s = 10.F;
	sample.density_kg_m3 = 1.5F;
	sample.voltage_v = 24.F;
	sample.source = {1, 1, 1, 123, 0, 0, 0, 0, 1, 1, 0};
}

void refresh(AccelerationControlInput &input, uint64_t now)
{
	input.now_us = now;
	input.context.validator_receive_time_us = now;
	input.selected_state.attitude_timestamp_us = input.selected_state.attitude_sample_us = now;
	input.selected_state.rate_timestamp_us = input.selected_state.rate_sample_us = now;
	input.feedback.observed_us = input.feedback.timestamp_sample_us = now;
	input.feedback.deadline_us = now + 99001;
	condition(input.operating_condition, now);
}

AccelerationControlInput input(const VehicleConfiguration &c, uint64_t now, float thrust)
{
	AccelerationControlInput result{};
	result.configuration = fingerprint(c);
	result.evidence_deadline_us = kStart + 90000;
	result.request.protocol_version = research_acceleration::kRequestProtocolVersionV2;
	result.request.command_kind = research_acceleration::CommandKind::AccelerationAndAttitude;
	result.request.source = 1; result.request.session = 7; result.request.sequence = 1;
	result.request.producer_monotonic_us = result.request.producer_time_in_validator_domain_us = now - 100;
	result.request.producer_expiry_monotonic_us = result.request.expiry_time_in_validator_domain_us = kStart + 70000;
	result.request.clock_mapping_uncertainty_us = 10;
	result.request.desired_attitude_ned_from_body = Quaternion(1.F, 0.F, 0.F, 0.F);
	result.request.acceleration_frd_m_s2 = Vector3f(thrust, 0.F, c.feedback.gravity_m_s2);
	result.context = {now, research_acceleration::ArmState::Armed, research_acceleration::ResearchModeState::Selected, true, true, true};
	result.selected_state.valid = true;
	// A separate real observer has its own epoch counter. Equality to the
	// Manual observer's local epoch is deliberately not manufactured here.
	result.selected_state.source_epoch = 2;
	result.selected_state.attitude = Quaternion(1.F, 0.F, 0.F, 0.F);
	result.feedback.source_generation = result.feedback.publisher_revision = 1;
	result.feedback.device_id = 456; result.feedback.state_source_epoch = 2;
	result.feedback.acceleration_frd_m_s2 = result.request.acceleration_frd_m_s2;
	refresh(result, now);
	return result;
}

// Memory-only successful callback handling. It uses the real command codec,
// bank accept/sample/confirm ticket and ACK codec; no physical IO occurs.
bool handle(ManualOutputBank &bank, uint64_t now, const EdfPermission &p, const OutputInventory &inventory,
	    const ManualOutputBinding &binding, const ManualWorkerOutput &frame, ManualOutputAck &ack)
{
	slaaps_manual_command_s command{};
	ManualWorkerOutput decoded{};

	if (!encodeManualCommand(now, frame, command) || !decodeManualCommand(now, command, decoded)
	    || bank.accept(now, p, inventory, binding, decoded) != ManualBankFault::None) { return false; }

	const auto ticket = bank.sample(now, p, inventory, binding);

	if (!ticket.command_valid || bank.confirmHandled(now + 1, p, inventory, binding, ticket, true, ack) != ManualBankFault::None) { return false; }

	slaaps_manual_ack_s wire{};
	ManualOutputAck decoded_ack{};

	if (!encodeManualAck(now + 1, ack.bank, ack, wire) || !decodeManualAck(now + 1, ack.bank, wire, decoded_ack)) { return false; }

	ack = decoded_ack;
	return true;
}

struct Rig {
	Rig(const VehicleConfiguration &profile, const OutputInventory &outputs) : c(profile), inventory(outputs), worker(owner)
	{
		p.timestamp_us = p.session = kStart;
		p.deadline_us = kStart + 100000;
		p.source = 17; p.configuration = fingerprint(c); p.mode = ControlMode::Manual;
		p.armed = p.flight_admitted = p.healthy = p.measured_condition_valid = true;
		raw.valid = true; raw.source = research_actuator_command::ManualControlSource::Mavlink;
		raw.mavlink_instance = c.manual_instance; raw.source_system_id = c.manual_system; raw.source_component_id = c.manual_component;
		state.valid = true; state.source_epoch = 1; state.attitude = Quaternion(1.F, 0.F, 0.F, 0.F);
	}
	void stamp(uint64_t now, float throttle)
	{
		raw.timestamp_us = raw.timestamp_sample_us = now; raw.axes[3] = throttle;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
		condition(operating, now);
	}
	bool begin()
	{
		stamp(kStart, -1.F);
		return worker.begin(kStart, c, p, raw, state, binding, &operating) == ManualWorkerStatus::Ready
		       && main.begin(kStart, 0, c, inventory, binding, p) == ManualBankFault::None
		       && aux.begin(kStart, 1, c, inventory, binding, p) == ManualBankFault::None;
	}
	bool manual(uint64_t now, float throttle)
	{
		stamp(now, throttle);
		ManualOutputAck a{}, b{};
		return worker.cycle(now, p, raw, state, frame, binding, nullptr, &operating) == ManualWorkerStatus::Command
		       && handle(main, now + 100, p, inventory, binding, frame, a)
		       && handle(aux, now + 200, p, inventory, binding, frame, b)
		       && worker.monitorTransfer(now + 300, p, binding, &a) == ManualWorkerStatus::Waiting
		       && worker.monitorTransfer(now + 300, p, binding, &b) == ManualWorkerStatus::Ready;
	}
	bool prime() { return begin() && manual(kStart + 4000, -1.F) && manual(kStart + 8000, -.5F) && manual(kStart + 12000, -.5F); }
	EdfPermission accelerationPermission() const
	{
		auto result = p;
		result.timestamp_us = kStart + 16000;
		result.transition_us = kEdge;
		result.mode = ControlMode::Acceleration;
		return result;
	}
	const VehicleConfiguration &c;
	const OutputInventory &inventory;
	ManualOutputBinding binding{42, {51, 52}, 3};
	EdfOwner owner{};
	ManualWorker worker;
	ManualOutputBank main{}, aux{};
	EdfPermission p{};
	ManualSample raw{};
	StateSample state{};
	OperatingConditionSample operating{};
	ManualWorkerOutput frame{};
};
}

slaaps::VehicleConfiguration slaapsFlightTransferConfiguration(const slaaps::VehicleConfiguration &base)
{
	return configuration(base);
}

bool slaapsFlightTransferTests(unsigned &checks, const slaaps::VehicleConfiguration &base, const slaaps::OutputInventory &inventory)
{
	bool passed = true;
	auto check = [&](bool good, const char *name) { ++checks; if (!good) { std::fprintf(stderr, "FAILED: flight transfer %s\n", name); passed = false; } };
	const auto c = slaapsFlightTransferConfiguration(base);
	const auto validation = validateAccelerationControlConfiguration(c);

	if (!validation.valid()) {
		std::fprintf(stderr, "flight transfer synthetic profile fault=%u parameter=%s\n", static_cast<unsigned>(validation.fault),
			     validation.parameter ? validation.parameter : "unknown");
	}

	check(validation.valid(), "explicit synthetic coupled profile completeness");

	if (!validation.valid()) { return false; }

	auto rig = std::make_unique<Rig>(c, inventory);
	check(rig->prime(), "Manual produces three genuinely handled combined frames");
	ManualWorkerOutput previous{};
	uint64_t handled_us{};
	check(rig->worker.handledOutput(previous, handled_us) && previous.sequence == 3 && handled_us == kStart + 12201
	      && std::fabs(previous.edf.normalized - .25F) < 1.e-6F, "history retains actual canards EDF and maximum bank ACK time");
	const uint64_t original_deadline = previous.deadline_us;
	auto next = rig->accelerationPermission();
	check(!rig->main.sample(next.timestamp_us, next, inventory, rig->binding).command_valid
	      && !rig->aux.sample(next.timestamp_us, next, inventory, rig->binding).command_valid
	      && rig->main.fault() == ManualBankFault::None && rig->aux.fault() == ManualBankFault::None,
	      "permission before transfer frame waits without a write or fault");
	float thrust{};
	check(commandToThrust(c, previous.edf.normalized, thrust), "shared calibrated current EDF coordinate");
	auto in = input(c, kStart + 17000, thrust);
	check(rig->worker.transferAcceleration(in.now_us, kEdge, c, rig->p, next, in, rig->binding, rig->frame) == ManualWorkerStatus::Command
	      && rig->frame.transition_us == kEdge && rig->frame.previous_handled_sequence == previous.sequence
	      && rig->frame.previous_deadline_us == original_deadline && rig->frame.deadline_us <= original_deadline
	      && rig->frame.edf.sequence > previous.edf.sequence && rig->frame.edf.session == previous.edf.session
	      && std::fabs(rig->frame.edf.normalized - previous.edf.normalized) < 1.e-6F,
	      "Manual to Acceleration preserves sampled EDF session and original outgoing bound");
	const auto calculated_diagnostic = rig->worker.accelerationDiagnostic();
	check(calculated_diagnostic.state == AccelerationDiagnosticState::Calculated && calculated_diagnostic.session == next.session
	      && calculated_diagnostic.calculated_sequence == rig->frame.sequence
	      && calculated_diagnostic.calculated_deadline_us == rig->frame.deadline_us && !calculated_diagnostic.handled_sequence,
	      "calculated acceleration diagnostic cannot claim handling before either bank ACK");
	ManualOutputAck a{}, b{};
	check(handle(rig->main, kStart + 17200, next, inventory, rig->binding, rig->frame, a)
	      && handle(rig->aux, kStart + 17300, next, inventory, rig->binding, rig->frame, b), "both physical banks consume the exact mode transfer");
	refresh(in, kStart + 17400);
	check(rig->worker.cycleAcceleration(in.now_us, next, in, rig->frame, rig->binding, &a) == ManualWorkerStatus::Waiting
	      && rig->worker.handledSequence() == previous.sequence && rig->worker.outstanding(), "one bank cannot commit numerical or handled history");
	check(!rig->worker.accelerationDiagnostic().handled_sequence
	      && rig->worker.accelerationDiagnostic().calculated_sequence == calculated_diagnostic.calculated_sequence,
	      "partial ACK preserves the separately calculated diagnostic identity");
	check(rig->worker.cycleAcceleration(in.now_us, next, in, rig->frame, rig->binding, &b) == ManualWorkerStatus::NotDue
	      && rig->worker.handledOutput(previous, handled_us) && previous.sequence == 4 && handled_us == b.accepted_us,
	      "last exact ACK commits true handling time and immediate cycle is NotDue");
	const auto acknowledged_diagnostic = rig->worker.accelerationDiagnostic();
	check(acknowledged_diagnostic.state == AccelerationDiagnosticState::Handled && acknowledged_diagnostic.handled_sequence == previous.sequence
	      && acknowledged_diagnostic.handled_us == b.accepted_us && acknowledged_diagnostic.handled_deadline_us == previous.deadline_us
	      && acknowledged_diagnostic.handled_moment_mask == calculated_diagnostic.calculated_moment_mask
	      && acknowledged_diagnostic.handled_canard_mask == calculated_diagnostic.calculated_canard_mask,
	      "only all-bank completion promotes exact time deadline and modeled masks to handled diagnostics");
	const auto held_request = in.request;
	refresh(in, kStart + 21400);
	check(rig->worker.cycleAcceleration(in.now_us, next, in, rig->frame, rig->binding) == ManualWorkerStatus::Command
	      && rig->frame.sequence == 5 && rig->frame.edf.request_timestamp_us == in.now_us
	      && rig->frame.previous_handled_sequence == 0 && rig->frame.transition_us == kEdge
	      && rig->frame.deadline_us > original_deadline && in.request.sequence == held_request.sequence
	      && in.request.expiry_time_in_validator_domain_us == held_request.expiry_time_in_validator_domain_us,
	      "fresh closed-loop target under held live ingress uses continuous EDF sequence without changing ingress expiry");
	check(handle(rig->main, kStart + 21500, next, inventory, rig->binding, rig->frame, a)
	      && handle(rig->aux, kStart + 21600, next, inventory, rig->binding, rig->frame, b),
	      "later acceleration command reaches both calibrated banks");
	refresh(in, kStart + 21700);
	check(rig->worker.cycleAcceleration(in.now_us, next, in, rig->frame, rig->binding, &a) == ManualWorkerStatus::Waiting
	      && rig->worker.cycleAcceleration(in.now_us, next, in, rig->frame, rig->binding, &b) == ManualWorkerStatus::NotDue,
	      "later frame also needs exact all-bank handling before integration");
	check(rig->worker.handledOutput(previous, handled_us), "outgoing acceleration history available");
	auto returning = next;
	returning.mode = ControlMode::Manual; returning.transition_us = kStart + 23000; returning.timestamp_us = kStart + 27000;
	rig->stamp(kStart + 28000, -.5F);
	check(rig->worker.transferManual(kStart + 28000, returning.transition_us, c, next, returning, rig->raw, rig->state,
					 rig->binding, rig->frame, &rig->operating) == ManualWorkerStatus::Command
	      && rig->frame.previous_handled_sequence == previous.sequence && rig->frame.previous_deadline_us == previous.deadline_us
	      && rig->frame.deadline_us <= previous.deadline_us
	      && std::memcmp(rig->frame.canards.data(), previous.canards.data(), sizeof(float) * kCanards) == 0
	      && std::fabs(rig->frame.edf.normalized - previous.edf.normalized) < 1.e-6F,
	      "centered reverse entry starts from actual handled canards and continuous EDF rather than trim or low restart");
	check(handle(rig->main, kStart + 28100, returning, inventory, rig->binding, rig->frame, a)
	      && handle(rig->aux, kStart + 28200, returning, inventory, rig->binding, rig->frame, b),
	      "reverse frame reaches both banks under the next exact marker");

	// D1 after companion loss in Acceleration, then the D2 exclusive handoff to Manual from that hold.
	auto hc = c;
	hc.recovery.hold_s = .02F;
	hc.recovery.ramp = hc.edf_fall;
	hc.recovery.thrust = 0.F;
	auto holder = std::make_unique<Rig>(hc, inventory);
	check(holder->prime(), "D1/D2 fixture primes genuine Manual history");
	auto hnext = holder->accelerationPermission();
	ManualWorkerOutput hprevious{};
	uint64_t hhandled{};
	float hthrust{};
	check(holder->worker.handledOutput(hprevious, hhandled) && commandToThrust(hc, hprevious.edf.normalized, hthrust),
	      "D1/D2 fixture has a handled EDF coordinate");
	auto hin = input(hc, kStart + 17000, hthrust);
	check(holder->worker.transferAcceleration(hin.now_us, kEdge, hc, holder->p, hnext, hin, holder->binding, holder->frame)
	      == ManualWorkerStatus::Command
	      && handle(holder->main, kStart + 17200, hnext, inventory, holder->binding, holder->frame, a)
	      && handle(holder->aux, kStart + 17300, hnext, inventory, holder->binding, holder->frame, b), "D1/D2 fixture enters Acceleration");
	refresh(hin, kStart + 17400);
	check(holder->worker.cycleAcceleration(hin.now_us, hnext, hin, holder->frame, holder->binding, &a) == ManualWorkerStatus::Waiting
	      && holder->worker.cycleAcceleration(hin.now_us, hnext, hin, holder->frame, holder->binding, &b) == ManualWorkerStatus::NotDue,
	      "D1/D2 fixture has a handled Acceleration frame");
	check(holder->worker.handledOutput(hprevious, hhandled), "D1/D2 fixture keeps the handled Acceleration frame");
	holder->stamp(kStart + 22000, -.5F);
	check(holder->worker.hold(kStart + 22000, hnext, holder->state, &holder->operating) == ManualWorkerStatus::Ready
	      && holder->worker.holding() && holder->worker.holdStart() == kStart + 22000,
	      "D1: companion loss holds in the same Acceleration session");
	check(holder->worker.accelerationDiagnostic().state == AccelerationDiagnosticState::Rejected
	      && holder->worker.accelerationDiagnostic().worker_status == ManualWorkerStatus::AccelerationRejected,
	      "D1: the hold records the lost companion demand in the acceleration diagnostic");
	check(holder->worker.holdCycle(kStart + 22000, hnext, holder->state, holder->frame, holder->binding, nullptr, &holder->operating)
	      == ManualWorkerStatus::Command && holder->frame.mode == ControlMode::Acceleration
	      && holder->frame.edf.disposition == EdfDisposition::Command && holder->frame.transition_us == kEdge
	      && std::fabs(holder->frame.edf.normalized - hprevious.edf.normalized) < 1.e-4F,
	      "D1: the first hold frame keeps the session mode and the current EDF");
	check(handle(holder->main, kStart + 22100, hnext, inventory, holder->binding, holder->frame, a)
	      && handle(holder->aux, kStart + 22200, hnext, inventory, holder->binding, holder->frame, b)
	      && holder->worker.holdCycle(kStart + 22300, hnext, holder->state, holder->frame, holder->binding, &a, &holder->operating)
	      == ManualWorkerStatus::Waiting
	      && holder->worker.holdCycle(kStart + 22300, hnext, holder->state, holder->frame, holder->binding, &b, &holder->operating)
	      == ManualWorkerStatus::NotDue, "D1: both banks handle the hold frame as an ordinary command");
	check(holder->worker.handledOutput(hprevious, hhandled) && hprevious.sequence == holder->frame.sequence,
	      "D1: hold frames become the handled history a D2 transfer seeds from");
	auto hreturning = hnext;
	hreturning.mode = ControlMode::Manual;
	hreturning.transition_us = kStart + 24000;
	hreturning.timestamp_us = kStart + 27000;
	holder->stamp(kStart + 28000, -.5F);
	check(holder->worker.transferManual(kStart + 28000, hreturning.transition_us, hc, hnext, hreturning, holder->raw, holder->state,
					    holder->binding, holder->frame, &holder->operating) == ManualWorkerStatus::Command
	      && !holder->worker.holding() && holder->worker.mode() == ControlMode::Manual
	      && holder->frame.previous_handled_sequence == hprevious.sequence
	      && std::memcmp(holder->frame.canards.data(), hprevious.canards.data(), sizeof(float) * kCanards) == 0,
	      "D2: the exclusive transfer leaves the hold for Manual from the handled hold frame");

	// An infeasible companion demand with healthy state and banks is a D1 hold, never D3.
	auto infeasible = std::make_unique<Rig>(hc, inventory);
	check(infeasible->prime(), "infeasible-demand fixture primes Manual");
	auto inext = infeasible->accelerationPermission();
	ManualWorkerOutput iprevious{};
	uint64_t ihandled{};
	float ithrust{};
	check(infeasible->worker.handledOutput(iprevious, ihandled) && commandToThrust(hc, iprevious.edf.normalized, ithrust),
	      "infeasible-demand fixture has a handled EDF coordinate");
	auto iin = input(hc, kStart + 17000, ithrust);
	check(infeasible->worker.transferAcceleration(iin.now_us, kEdge, hc, infeasible->p, inext, iin, infeasible->binding, infeasible->frame)
	      == ManualWorkerStatus::Command
	      && handle(infeasible->main, kStart + 17200, inext, inventory, infeasible->binding, infeasible->frame, a)
	      && handle(infeasible->aux, kStart + 17300, inext, inventory, infeasible->binding, infeasible->frame, b),
	      "infeasible-demand fixture enters Acceleration");
	refresh(iin, kStart + 17400);
	check(infeasible->worker.cycleAcceleration(iin.now_us, inext, iin, infeasible->frame, infeasible->binding, &a) == ManualWorkerStatus::Waiting
	      && infeasible->worker.cycleAcceleration(iin.now_us, inext, iin, infeasible->frame, infeasible->binding, &b) == ManualWorkerStatus::NotDue,
	      "infeasible-demand fixture has a handled Acceleration frame");
	refresh(iin, kStart + 22000);
	iin.request.acceleration_frd_m_s2(1) = 50.F;
	const auto infeasible_status = infeasible->worker.cycleAcceleration(iin.now_us, inext, iin, infeasible->frame, infeasible->binding);
	const auto &infeasible_detail = infeasible->worker.accelerationDiagnostic();
	check(infeasible_status == ManualWorkerStatus::Waiting && infeasible->worker.holding() && !infeasible->worker.recovering()
	      && infeasible_detail.state == AccelerationDiagnosticState::Rejected
	      && infeasible_detail.fault == AccelerationControlFault::AllocationRejected,
	      "D1: an infeasible demand holds attitude with the allocator rejection retained in diagnostics");

	auto crossing = std::make_unique<Rig>(c, inventory);
	check(crossing->prime(), "mode-edge pending-ticket fixture has genuine outgoing history");
	crossing->stamp(kStart + 16000, -.5F);
	check(crossing->worker.cycle(kStart + 16000, crossing->p, crossing->raw, crossing->state, crossing->frame,
				     crossing->binding, nullptr, &crossing->operating) == ManualWorkerStatus::Command
	      && crossing->main.accept(kStart + 16100, crossing->p, inventory, crossing->binding, crossing->frame) == ManualBankFault::None
	      && crossing->aux.accept(kStart + 16101, crossing->p, inventory, crossing->binding, crossing->frame) == ManualBankFault::None,
	      "both banks accept the exact outstanding frame before the actual mode edge");
	constexpr uint64_t pending_edge = kStart + 16500;
	check(crossing->main.canCompleteAtTransition(pending_edge) && crossing->aux.canCompleteAtTransition(pending_edge)
	      && !crossing->main.canCompleteAtTransition(kStart + 16099), "only an already admitted pre-edge ticket may finish");
	const auto main_ticket = crossing->main.sample(kStart + 16600, crossing->p, inventory, crossing->binding);
	const auto aux_ticket = crossing->aux.sample(kStart + 16600, crossing->p, inventory, crossing->binding);
	check(crossing->main.confirmHandled(kStart + 16700, crossing->p, inventory, crossing->binding, main_ticket, true,
					    a) == ManualBankFault::None
	      && crossing->aux.confirmHandled(kStart + 16701, crossing->p, inventory, crossing->binding, aux_ticket, true, b) == ManualBankFault::None
	      && crossing->worker.monitorTransfer(kStart + 16800, crossing->p, crossing->binding, &a) == ManualWorkerStatus::Waiting
	      && crossing->worker.monitorTransfer(kStart + 16800, crossing->p, crossing->binding, &b) == ManualWorkerStatus::Ready,
	      "post-edge handling ACKs drain without producing another outgoing frame");
	auto crossing_permission = crossing->accelerationPermission();
	crossing_permission.timestamp_us = kStart + 20500;
	crossing_permission.transition_us = pending_edge;
	auto crossing_input = input(c, kStart + 21000, thrust);
	check(crossing->worker.handledOutput(previous, handled_us) && handled_us > pending_edge
	      && crossing->worker.transferAcceleration(crossing_input.now_us, pending_edge, c, crossing->p, crossing_permission,
			      crossing_input, crossing->binding, crossing->frame) == ManualWorkerStatus::Command,
	      "true post-edge ACK time is preserved rather than fabricated as the transition time");

	auto waiting = std::make_unique<Rig>(c, inventory);
	check(waiting->prime(), "permission-wait expiry fixture has handled output");
	const auto waiting_permission = waiting->accelerationPermission();
	check(!waiting->main.sample(waiting_permission.timestamp_us, waiting_permission, inventory, waiting->binding).command_valid
	      && !waiting->main.sample(waiting->worker.outputDeadline(), waiting_permission, inventory, waiting->binding).command_valid
	      && waiting->main.fault() == ManualBankFault::Expired, "waiting for the matching command cannot extend the old bank deadline");

	auto empty = std::make_unique<Rig>(c, inventory);
	check(empty->begin(), "empty-history fixture has ownership but no handled frame");
	ManualWorkerOutput no_history{}; no_history.sequence = 999;
	auto no_history_input = input(c, kStart + 8000, thrust);
	auto no_history_permission = empty->accelerationPermission();
	no_history_permission.timestamp_us = kStart + 7000;
	no_history_permission.transition_us = kStart + 5000;
	check(empty->worker.transferAcceleration(no_history_input.now_us, no_history_permission.transition_us, c, empty->p, no_history_permission,
			no_history_input, empty->binding, no_history) == ManualWorkerStatus::HistoryUnavailable && no_history.sequence == 999,
	      "a fresh owner grant cannot fabricate canard handling history");

	for (unsigned fault = 0; fault < 8; ++fault) {
		auto rejected = std::make_unique<Rig>(c, inventory);
		check(rejected->prime(), "negative transfer fixture has real handled history");
		auto permission = rejected->accelerationPermission();
		auto request = input(c, kStart + 17000, thrust);
		auto binding = rejected->binding;
		uint64_t edge = kEdge;

		switch (fault) {
		case 0: ++permission.transition_us; break;

		case 1: ++permission.source; break;

		case 2: request.request.producer_time_in_validator_domain_us = edge; break;

		case 3: ++request.selected_state.attitude_reset_counter; break;

		case 4: ++binding.driver[1]; break;

		case 5: permission.healthy = false; break;

		case 6: permission.deadline_us = request.now_us; break;

		case 7: edge = 0; break;
		}

		ManualWorkerOutput sentinel{}; sentinel.sequence = 999;
		check(rejected->worker.transferAcceleration(request.now_us, edge, c, rejected->p, permission, request, binding, sentinel)
		      != ManualWorkerStatus::Command && sentinel.sequence == 999 && !rejected->worker.handledOutput(previous, handled_us),
		      "wrong mode token source state input age or lifecycle latches failure and preserves destination");
	}

	for (unsigned fault = 0; fault < 5; ++fault) {
		auto rejected = std::make_unique<Rig>(c, inventory);
		check(rejected->prime(), "negative bank fixture has actual prior handling");
		auto permission = rejected->accelerationPermission();
		auto request = input(c, kStart + 17000, thrust);
		check(rejected->worker.transferAcceleration(request.now_us, kEdge, c, rejected->p, permission, request,
				rejected->binding, rejected->frame) == ManualWorkerStatus::Command, "negative bank fixture obtains real transferred command");
		auto frame = rejected->frame;

		switch (fault) {
		case 0: ++frame.previous_handled_sequence; break;

		case 1: ++frame.previous_deadline_us; break;

		case 2: ++frame.transition_us; break;

		case 3: frame.deadline_us = frame.previous_deadline_us + 1; break;

		case 4: ++frame.binding.driver[0]; break;
		}

		check(rejected->main.accept(kStart + 17100, permission, inventory, rejected->binding, frame) != ManualBankFault::None
		      && rejected->main.accept(kStart + 17200, permission, inventory, rejected->binding, rejected->frame) != ManualBankFault::None,
		      "bank rejects changed prior sequence expiry marker or physical identity and never rolls back");
	}

	auto expiry = std::make_unique<Rig>(c, inventory);
	check(expiry->prime(), "ingress expiry fixture history");
	next = expiry->accelerationPermission();
	in = input(c, kStart + 17000, thrust);
	in.request.producer_expiry_monotonic_us = in.request.expiry_time_in_validator_domain_us = kStart + 23010;
	check(expiry->worker.transferAcceleration(in.now_us, kEdge, c, expiry->p, next, in, expiry->binding,
			expiry->frame) == ManualWorkerStatus::Command,
	      "bounded authenticated ingress starts a transferred control frame");
	check(handle(expiry->main, kStart + 17200, next, inventory, expiry->binding, expiry->frame, a)
	      && handle(expiry->aux, kStart + 17300, next, inventory, expiry->binding, expiry->frame, b), "expiry fixture exact ACKs");
	refresh(in, kStart + 17400);
	check(expiry->worker.cycleAcceleration(in.now_us, next, in, expiry->frame, expiry->binding, &a) == ManualWorkerStatus::Waiting
	      && expiry->worker.cycleAcceleration(in.now_us, next, in, expiry->frame, expiry->binding, &b) == ManualWorkerStatus::NotDue,
	      "expiry fixture commits only after all banks");
	refresh(in, kStart + 21400);
	check(expiry->worker.cycleAcceleration(in.now_us, next, in, expiry->frame, expiry->binding) == ManualWorkerStatus::Command
	      && expiry->frame.deadline_us == kStart + 23000, "held original ingress caps a later feedback target at conservative expiry");
	refresh(in, kStart + 23000);
	ManualWorkerOutput sentinel{}; sentinel.sequence = 999;
	check(expiry->worker.cycleAcceleration(in.now_us, next, in, sentinel, expiry->binding) != ManualWorkerStatus::Command
	      && sentinel.sequence == 999
	      && !expiry->worker.handledOutput(previous, handled_us), "fresh state and permission cannot revive expired held ingress");

	ManualOutputBank cold;
	// A disposable real-worker/bank rig isolates malformed ACK diagnostics from
	// successful handoff and expiry cases above. The small explicit moment limit
	// makes rotational clipping visible without claiming measured saturation.
	auto clipped = c;
	clipped.axis[0].moment_limit = 1.e-4F;
	auto bad_ack = std::make_unique<Rig>(clipped, inventory);
	check(bad_ack->prime(), "malformed ACK diagnostic fixture has actual Manual handling");
	auto bad_permission = bad_ack->accelerationPermission();
	auto bad_input = input(clipped, kStart + 17000, thrust);
	bad_input.request.desired_attitude_ned_from_body = Quaternion(0.F, 1.F, 0.F, 0.F);
	check(bad_ack->worker.transferAcceleration(bad_input.now_us, kEdge, clipped, bad_ack->p, bad_permission,
			bad_input, bad_ack->binding, bad_ack->frame) == ManualWorkerStatus::Command,
	      "clipped calculated demand still passes configured physical residual bounds");
	const auto clipped_calculation = bad_ack->worker.accelerationDiagnostic();
	check(clipped_calculation.calculated_moment_mask && !clipped_calculation.handled_sequence,
	      "modeled rotational clipping is visible as calculated before handling");
	check(handle(bad_ack->main, kStart + 17200, bad_permission, inventory, bad_ack->binding, bad_ack->frame, a)
	      && handle(bad_ack->aux, kStart + 17300, bad_permission, inventory, bad_ack->binding, bad_ack->frame, b),
	      "clipped command reaches both exact bank callbacks");
	refresh(bad_input, kStart + 17400);
	check(bad_ack->worker.cycleAcceleration(bad_input.now_us, bad_permission, bad_input, bad_ack->frame, bad_ack->binding, &a)
	      == ManualWorkerStatus::Waiting
	      && bad_ack->worker.cycleAcceleration(bad_input.now_us, bad_permission, bad_input, bad_ack->frame, bad_ack->binding, &b)
	      == ManualWorkerStatus::NotDue, "clipped command also waits for both exact ACKs");
	const auto before_bad_ack = bad_ack->worker.accelerationDiagnostic();
	check(before_bad_ack.handled_moment_mask == clipped_calculation.calculated_moment_mask,
	      "handled modeled clipping is copied only from the acknowledged calculation");
	refresh(bad_input, kStart + 21400);
	check(bad_ack->worker.cycleAcceleration(bad_input.now_us, bad_permission, bad_input, bad_ack->frame, bad_ack->binding)
	      == ManualWorkerStatus::Command
	      && handle(bad_ack->main, kStart + 21500, bad_permission, inventory, bad_ack->binding, bad_ack->frame, a),
	      "malformed ACK case starts from a genuine newly handled bank reply");
	++a.sequence;
	refresh(bad_input, kStart + 21600);
	check(bad_ack->worker.cycleAcceleration(bad_input.now_us, bad_permission, bad_input, bad_ack->frame, bad_ack->binding, &a)
	      == ManualWorkerStatus::OutputRejected, "malformed driver ACK is an output failure rather than a numerical rejection");
	const auto after_bad_ack = bad_ack->worker.accelerationDiagnostic();
	check(after_bad_ack.state == AccelerationDiagnosticState::Rejected && after_bad_ack.fault == AccelerationControlFault::None
	      && after_bad_ack.allocation_stage == AccelerationAllocationStage::None && after_bad_ack.fault_us == bad_input.now_us
	      && after_bad_ack.handled_sequence == before_bad_ack.handled_sequence && after_bad_ack.handled_us == before_bad_ack.handled_us
	      && after_bad_ack.handled_deadline_us == before_bad_ack.handled_deadline_us
	      && after_bad_ack.handled_moment_mask == before_bad_ack.handled_moment_mask,
	      "failed ACK retains previous handled proof and never invents an allocation fault");
	bad_ack->worker.stop();
	check(bad_ack->worker.accelerationDiagnostic().fault_us == after_bad_ack.fault_us
	      && bad_ack->worker.accelerationDiagnostic().state == AccelerationDiagnosticState::Rejected,
	      "ordinary cleanup cannot erase retained output rejection evidence");

	auto cold_permission = next;
	cold_permission.session = cold_permission.timestamp_us = kStart;
	cold_permission.transition_us = 0;
	check(cold.begin(kStart, 0, c, inventory, rig->binding, cold_permission) != ManualBankFault::None,
	      "cold acceleration cannot invent initial handled output history");
	rig->worker.stop();
	check(!rig->worker.handledOutput(previous, handled_us) && rig->worker.handledSequence() == 0,
	      "stopped worker exposes no reusable handled authority");
	return passed;
}
