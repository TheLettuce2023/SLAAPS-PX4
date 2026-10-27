#include <lib/slaaps/EdfOwner.hpp>
#include <lib/slaaps/ManualEdfInput.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

// Existing host/native runner supplies its explicitly synthetic accepted model.
bool slaapsEdfOwnerTransferTests(unsigned &checks, const slaaps::VehicleConfiguration &configuration)
{
	using namespace slaaps;
	bool passed = true;
	auto expect = [&](bool condition, const char *description) {
		++checks;

		if (!condition) { std::fprintf(stderr, "FAIL EDF transfer: %s\n", description); passed = false; }
	};
	auto c = configuration;
	c.edf_rise = 2.F;
	c.edf_fall = 3.F;
	c.output_lease_ms = 100;
	constexpr uint64_t start = 1000000, decision = start + 31000, transition = start + 30000;
	const uint64_t identity = fingerprint(c);
	EdfPermission first{};
	first.timestamp_us = first.session = start;
	first.deadline_us = start + 100000;
	first.source = 17;
	first.configuration = identity;
	first.mode = ControlMode::Manual;
	first.armed = first.flight_admitted = first.healthy = first.measured_condition_valid = true;
	EdfRequest initial{start, 17, identity, 1, start, start + 100000, ControlMode::Manual, EdfUnits::Normalized, 0.F};
	auto outgoing = initial;
	outgoing.sequence = 2;
	outgoing.timestamp_us = start + 1000;
	outgoing.deadline_us = start + 90000;
	outgoing.value = .8F;
	auto previous = first;
	previous.timestamp_us = start + 20000;
	previous.deadline_us = outgoing.deadline_us;
	auto next = previous;
	next.timestamp_us = decision;
	next.mode = ControlMode::Acceleration;
	next.transition_us = transition;
	float target_thrust{};
	expect(commandToThrust(c, .6F, target_thrust), "shared calibrated curve converts the incoming Newton target");
	EdfRequest incoming{start, 17, identity, 3, transition + 1, outgoing.deadline_us,
			    ControlMode::Acceleration, EdfUnits::Newtons, target_thrust};
	auto prime = [&](EdfOwner & owner) {
		const bool begun = owner.begin(start, c, first) == EdfFault::None
				   && owner.accept(start, first, initial) == EdfFault::None
				   && owner.accept(start + 1000, first, outgoing) == EdfFault::None;
		const auto sampled = owner.sample(start + 21000, previous);
		expect(begun && sampled.disposition == EdfDisposition::Command && std::fabs(sampled.normalized - .04F) < 1.e-6F,
		       "outgoing flight owner has a real nonzero slew-limited current sample");
	};
	EdfOwner owner, reference;
	prime(owner);
	prime(reference);
	const auto uninterrupted = reference.sample(decision, previous);
	EdfCommand current{};
	expect(owner.transfer(decision, transition, previous, next, incoming, current) == EdfFault::None
	       && std::memcmp(&current.normalized, &uninterrupted.normalized, sizeof(float)) == 0
	       && std::fabs(current.normalized - .06F) < 1.e-6F && current.deadline_us == outgoing.deadline_us
	       && current.session == first.session && current.source == first.source && current.configuration == identity
	       && current.sequence == incoming.sequence && current.request_timestamp_us == incoming.timestamp_us,
	       "Manual-to-Acceleration preserves the uninterrupted sampled output and exact outgoing lease under a fresh request");
	const auto same_time = owner.sample(decision, next);
	const auto later = owner.sample(start + 41000, next);
	expect(std::fabs(same_time.normalized - .06F) < 1.e-6F && std::fabs(later.normalized - .08F) < 1.e-6F,
	       "new target cannot spend pre-transfer time and begins normal slew only after transfer");
	auto acceleration = next;
	acceleration.timestamp_us = start + 41000;
	auto manual = acceleration;
	manual.mode = ControlMode::Manual;
	manual.timestamp_us = start + 51000;
	manual.transition_us = start + 50000;
	EdfRequest returning{start, 17, identity, 4, start + 50001, outgoing.deadline_us,
			     ControlMode::Manual, EdfUnits::Normalized, 0.F};
	expect(owner.transfer(start + 51000, start + 50000, acceleration, manual, returning, current) == EdfFault::None
	       && std::fabs(current.normalized - .10F) < 1.e-6F && current.deadline_us == outgoing.deadline_us,
	       "Acceleration-to-Manual also transfers a nonzero output without a low-throttle restart");
	expect(std::fabs(owner.sample(start + 61000, manual).normalized - .07F) < 1.e-6F,
	       "returning Manual target uses its own subsequent fall slew without an invented hold target");
	manual.timestamp_us = start + 61001;
	manual.deadline_us = start + 161001;
	auto fresh = returning;
	fresh.sequence = 5;
	fresh.timestamp_us = start + 61001;
	fresh.deadline_us = manual.deadline_us;
	fresh.value = .2F;
	expect(owner.accept(fresh.timestamp_us, manual, fresh) == EdfFault::None
	       && owner.sample(outgoing.deadline_us, manual).disposition == EdfDisposition::Command,
	       "an ordinary genuinely fresh later request may renew normally after the transfer");

	for (unsigned scenario = 0; scenario < 34; ++scenario) {
		EdfOwner rejected;
		prime(rejected);
		auto old_permission = previous, new_permission = next;
		auto request = incoming;
		uint64_t token = transition, now = decision;

		switch (scenario) {
		case 0: ++new_permission.session; break;

		case 1: ++new_permission.source; break;

		case 2: ++new_permission.configuration; break;

		case 3: new_permission.mode = ControlMode::Manual; break;

		case 4: new_permission.mode = ControlMode::EdfBench; break;

		case 5: new_permission.armed = false; break;

		case 6: new_permission.flight_admitted = false; break;

		case 7: new_permission.measured_condition_valid = false; break;

		case 8: new_permission.healthy = false; break;

		case 9: new_permission.kill = true; break;

		case 10: new_permission.lockdown = true; break;

		case 11: new_permission.termination = true; break;

		case 12: new_permission.commissioning_admitted = true; break;

		case 13: new_permission.timestamp_us = transition - 1; break;

		case 14: new_permission.deadline_us = now; break;

		case 15: ++old_permission.source; break;

		case 16: old_permission.mode = ControlMode::Acceleration; break;

		case 17: old_permission.deadline_us = now; break;

		case 18: request.sequence = outgoing.sequence; break;

		case 19: request.timestamp_us = transition; break;

		case 20: request.timestamp_us = outgoing.timestamp_us; break;

		case 21: ++request.deadline_us; break;

		case 22: --old_permission.deadline_us; break;

		case 23: --new_permission.deadline_us; break;

		case 24: ++request.source; break;

		case 25: request.mode = ControlMode::Manual; break;

		case 26: request.units = EdfUnits::Normalized; break;

		case 27: request.value = NAN; break;

		case 28: token = 0; break;

		case 29: token = now + 1; break;

		case 30: token = first.session; break;

		case 31: now = outgoing.deadline_us; break;

		case 32: ++new_permission.transition_us; break;

		case 33: ++old_permission.transition_us; break;
		}

		EdfCommand sentinel{};
		sentinel.normalized = .37F;
		sentinel.sequence = 99;
		unsigned char bytes[sizeof(sentinel)] {};
		std::memcpy(bytes, &sentinel, sizeof(sentinel));
		expect(rejected.transfer(now, token, old_permission, new_permission, request, sentinel) != EdfFault::None
		       && std::memcmp(bytes, &sentinel, sizeof(sentinel)) == 0
		       && rejected.sample(now, new_permission).disposition != EdfDisposition::Command,
		       "wrong identities/modes, permission loss, stale request, lease extension or bad transition revoke without changing destination");
	}

	EdfOwner expiry;
	prime(expiry);
	auto live_permission = next;
	live_permission.timestamp_us = start + 80000;
	live_permission.deadline_us = start + 180000;
	expect(expiry.transfer(decision, transition, previous, next, incoming, current) == EdfFault::None
	       && expiry.sample(outgoing.deadline_us, live_permission).disposition == EdfDisposition::RecoveryRequired,
	       "successful transfer expires at the original raw request boundary even with fresh new-mode permission");
	EdfOwner replay;
	prime(replay);
	expect(replay.transfer(decision, transition, previous, next, incoming, current) == EdfFault::None,
	       "a transition token is consumed once");
	unsigned char retained[sizeof(current)] {};
	std::memcpy(retained, &current, sizeof(current));
	expect(replay.transfer(start + 51000, transition, acceleration, manual, returning, current) != EdfFault::None
	       && std::memcmp(retained, &current, sizeof(current)) == 0,
	       "replaying a transfer token cannot restore the old mode");
	EdfOwner exclusive;
	prime(exclusive);
	expect(exclusive.transfer(decision, transition, previous, next, incoming, current) == EdfFault::None
	       && exclusive.sample(decision, previous).disposition != EdfDisposition::Command,
	       "the outgoing mode permission loses ownership immediately after successful transfer");
	EdfOwner initial_marker;
	auto marked_begin = first;
	marked_begin.transition_us = transition;
	expect(initial_marker.begin(start, c, marked_begin) == EdfFault::InvalidGrant,
	       "an initial grant cannot masquerade as an already transferred owner");
	EdfOwner empty;
	expect(empty.begin(start, c, first) == EdfFault::None
	       && empty.transfer(decision, transition, previous, next, incoming, current) != EdfFault::None,
	       "an owner without an accepted outgoing request cannot create transfer authority");

	auto back_grant = next;
	back_grant.mode = ControlMode::Manual;
	back_grant.timestamp_us = start + 51000;
	back_grant.transition_us = start + 50000;
	research_actuator_command::ManualControlSample raw{};
	raw.valid = true;
	raw.source = c.manual_source == static_cast<int32_t>(ManualInputSource::RcOnly) ?
		     research_actuator_command::ManualControlSource::Rc : research_actuator_command::ManualControlSource::Mavlink;
	raw.mavlink_instance = static_cast<uint8_t>(c.manual_instance);
	raw.source_system_id = static_cast<uint8_t>(c.manual_system);
	raw.source_component_id = static_cast<uint8_t>(c.manual_component);
	raw.timestamp_us = back_grant.timestamp_us;
	raw.timestamp_sample_us = back_grant.transition_us + 1;
	raw.axes[static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)] = c.manual_throttle_low;
	const uint64_t raw_lease = static_cast<uint64_t>(c.manual_age_ms < c.output_lease_ms ? c.manual_age_ms : c.output_lease_ms) * 1000;
	ManualEdfInput adapter;
	EdfRequest mapped{};
	expect(adapter.configureForTransfer(c, back_grant, back_grant.transition_us, incoming.sequence)
	       && adapter.evaluate(back_grant.timestamp_us, raw, mapped) == ManualEdfStatus::Request
	       && mapped.sequence == incoming.sequence + 1 && mapped.timestamp_us == raw.timestamp_sample_us
	       && mapped.deadline_us == raw.timestamp_sample_us + raw_lease && mapped.mode == ControlMode::Manual
	       && mapped.units == EdfUnits::Normalized && std::fabs(mapped.value) < 1.e-6F,
	       "post-edge Manual input preserves raw expiry and continues the worker EDF sequence");
	EdfOwner adapted_owner;
	prime(adapted_owner);
	auto capped = mapped;
	capped.deadline_us = outgoing.deadline_us;
	expect(adapted_owner.transfer(decision, transition, previous, next, incoming, current) == EdfFault::None
	       && adapted_owner.transfer(back_grant.timestamp_us, back_grant.transition_us, acceleration, back_grant, capped, current) == EdfFault::None
	       && current.sequence == mapped.sequence && current.request_timestamp_us == raw.timestamp_sample_us
	       && current.deadline_us == outgoing.deadline_us && mapped.deadline_us == raw.timestamp_sample_us + raw_lease,
	       "a capped transfer copy is compatible with exact owner markers without rewriting the adapter's original raw lease");
	unsigned char mapped_bytes[sizeof(mapped)] {};
	std::memcpy(mapped_bytes, &mapped, sizeof(mapped));
	raw.timestamp_us = back_grant.timestamp_us + 1000;
	expect(adapter.evaluate(raw.timestamp_us, raw, mapped) == ManualEdfStatus::Held
	       && std::memcmp(mapped_bytes, &mapped, sizeof(mapped)) == 0
	       && adapter.evaluate(raw.timestamp_sample_us + raw_lease, raw, mapped) == ManualEdfStatus::Rejected
	       && std::memcmp(mapped_bytes, &mapped, sizeof(mapped)) == 0,
	       "held publication cannot emit a fresh request or extend its exclusive original raw deadline");

	for (unsigned before = 0; before < 2; ++before) {
		ManualEdfInput stale;
		auto pre_edge = raw;
		pre_edge.timestamp_us = back_grant.timestamp_us;
		pre_edge.timestamp_sample_us = back_grant.transition_us - before;
		expect(stale.configureForTransfer(c, back_grant, back_grant.transition_us, incoming.sequence)
		       && stale.evaluate(back_grant.timestamp_us, pre_edge, mapped) == ManualEdfStatus::Rejected
		       && std::memcmp(mapped_bytes, &mapped, sizeof(mapped)) == 0,
		       "a retained sample at or before the transfer edge cannot become new Manual authority");
	}

	for (unsigned invalid = 0; invalid < 7; ++invalid) {
		ManualEdfInput bad;
		auto grant = back_grant;
		uint64_t marker = back_grant.transition_us, sequence = incoming.sequence;

		switch (invalid) {
		case 0: grant.transition_us = 0; break;

		case 1: ++grant.transition_us; break;

		case 2: marker = 0; break;

		case 3: marker = grant.session; break;

		case 4: marker = grant.timestamp_us + 1; break;

		case 5: sequence = 0; break;

		case 6: sequence = UINT64_MAX; break;
		}

		expect(!bad.configureForTransfer(c, grant, marker, sequence)
		       && bad.evaluate(back_grant.timestamp_us, raw, mapped) == ManualEdfStatus::NotConfigured,
		       "unbound transfer marker, pre-entry token or exhausted EDF sequence cannot configure Manual input");
	}

	ManualEdfInput sequence_end;
	raw.timestamp_us = back_grant.timestamp_us;
	expect(sequence_end.configureForTransfer(c, back_grant, back_grant.transition_us, UINT64_MAX - 1)
	       && sequence_end.evaluate(raw.timestamp_us, raw, mapped) == ManualEdfStatus::Request && mapped.sequence == UINT64_MAX,
	       "the final representable EDF sequence is emitted once without wrap");
	std::memcpy(mapped_bytes, &mapped, sizeof(mapped));
	++raw.timestamp_us;
	++raw.timestamp_sample_us;
	expect(sequence_end.evaluate(raw.timestamp_us, raw, mapped) == ManualEdfStatus::Rejected
	       && std::memcmp(mapped_bytes, &mapped, sizeof(mapped)) == 0,
	       "a further genuinely new raw sample rejects sequence wrap without replacing the request");
	ManualEdfInput timestamp_end;
	raw.timestamp_us = raw.timestamp_sample_us = UINT64_MAX - raw_lease + 1;
	expect(timestamp_end.configureForTransfer(c, back_grant, back_grant.transition_us, incoming.sequence)
	       && timestamp_end.evaluate(raw.timestamp_us, raw, mapped) == ManualEdfStatus::Rejected
	       && std::memcmp(mapped_bytes, &mapped, sizeof(mapped)) == 0,
	       "overflowing the original raw deadline rejects before arithmetic or request publication");
	return passed;
}

// D3/D4 owner mechanism with explicitly synthetic timing; canard freeze is a bank action tested elsewhere.
bool slaapsEdfOwnerRecoveryTests(unsigned &checks, const slaaps::VehicleConfiguration &configuration)
{
	using namespace slaaps;
	bool passed = true;
	auto expect = [&](bool condition, const char *description) {
		++checks;

		if (!condition) { std::fprintf(stderr, "FAIL EDF recovery: %s\n", description); passed = false; }
	};
	auto c = configuration;
	c.edf_rise = 2.F;
	c.edf_fall = 3.F;
	c.output_lease_ms = 100;
	constexpr uint64_t start = 1000000;
	const uint64_t identity = fingerprint(c);
	EdfPermission grant{};
	grant.timestamp_us = grant.session = start;
	grant.deadline_us = start + 100000;
	grant.source = 17;
	grant.configuration = identity;
	grant.mode = ControlMode::Manual;
	grant.armed = grant.flight_admitted = grant.healthy = grant.measured_condition_valid = true;
	EdfRequest initial{start, 17, identity, 1, start, start + 100000, ControlMode::Manual, EdfUnits::Normalized, 0.F};
	auto high = initial;
	high.sequence = 2;
	high.timestamp_us = start + 1000;
	high.deadline_us = start + 90000;
	high.value = .8F;
	auto prime = [&](EdfOwner & owner) {
		const bool ready = owner.begin(start, c, grant) == EdfFault::None
				   && owner.accept(start, grant, initial) == EdfFault::None
				   && owner.accept(start + 1000, grant, high) == EdfFault::None
				   && std::fabs(owner.sample(start + 89000, grant).normalized - .176F) < 1.e-5F;
		expect(ready, "recovery fixture has a real nonzero sampled output");
	};
	EdfOwner owner;
	expect(owner.recover(start, grant).disposition == EdfDisposition::Inhibited && !owner.recovering(),
	       "recovery cannot start without a lost flight grant");
	prime(owner);
	expect(owner.sample(start + 90000, grant).disposition == EdfDisposition::RecoveryRequired
	       && owner.fault() == EdfFault::Expired && owner.recovering(),
	       "strict sample still reports the loss and recovery begins");
	auto first = owner.recover(start + 91000, grant);
	expect(first.disposition == EdfDisposition::Recovery && std::fabs(first.normalized - .170F) < 1.e-5F
	       && first.deadline_us == start + 191000 && first.session == start && first.source == 17,
	       "recovery ramps down from the last sampled output at SLA_EDF_FALL under a fresh one-lease deadline");
	const auto mid = owner.recover(start + 111000, grant);
	expect(mid.disposition == EdfDisposition::Recovery && std::fabs(mid.normalized - .110F) < 1.e-5F,
	       "ramp is time based and never rises");
	expect(owner.recover(start + 160000, grant).normalized <= 0.F, "ramp reaches EDF off");
	EdfRequest late = high;
	late.sequence = 3;
	late.timestamp_us = start + 160001;
	late.deadline_us = start + 190000;
	expect(owner.accept(start + 161000, grant, late) != EdfFault::None && owner.sample(start + 161000, grant).disposition ==
	       EdfDisposition::RecoveryRequired, "recovery never accepts requests or revives the grant");
	const auto edge = owner.recover(start + 90000 + EdfOwner::kRecoveryCeilingUs - 1, grant);
	expect(edge.disposition == EdfDisposition::Recovery && edge.deadline_us == start + 90000 + EdfOwner::kRecoveryCeilingUs,
	       "deadline is capped by the 60 s ceiling");
	expect(owner.recover(start + 90000 + EdfOwner::kRecoveryCeilingUs, grant).disposition == EdfDisposition::Inhibited
	       && !owner.recovering() && owner.recover(start + 91000 + EdfOwner::kRecoveryCeilingUs, grant).disposition ==
	       EdfDisposition::Inhibited, "the ceiling ends recovery permanently");

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		EdfOwner other;
		prime(other);
		auto p = grant;
		p.timestamp_us = start + 50000;
		p.deadline_us = start + 100000;

		if (scenario == 0) { p.healthy = false; }

		other.sample(start + 90000, scenario == 0 ? p : grant);

		if (scenario == 0) {
			expect(other.fault() == EdfFault::PermissionLost && other.recovering(), "permission loss starts recovery");
		}

		auto q = grant;

		if (scenario == 1) { q.kill = true; }

		if (scenario == 2) { q.armed = false; }

		if (scenario == 3) { q.session += 1; }

		if (scenario == 4) { other.revoke(); }

		const auto out = other.recover(start + 91000, q);
		expect(scenario == 0 ? out.disposition == EdfDisposition::Recovery : out.disposition == EdfDisposition::Inhibited,
		       "healthy-permission loss recovers; kill, disarm, another session and revoke give no recovery output");
	}

	EdfOwner bench;
	auto bench_grant = grant;
	bench_grant.mode = ControlMode::EdfBench;
	bench_grant.flight_admitted = bench_grant.measured_condition_valid = false;
	bench_grant.commissioning_admitted = true;
	auto bench_config = c;
	bench_config.edf_commission_max = .3F;
	bench.begin(start, bench_config, bench_grant);
	bench_grant.healthy = false;
	bench.sample(start + 1000, bench_grant);
	expect(!bench.recovering() && bench.recover(start + 2000, bench_grant).disposition == EdfDisposition::Inhibited,
	       "bench commissioning never enters airborne recovery");
	EdfOwner replaced;
	prime(replaced);
	replaced.sample(start + 90000, grant);
	auto next = grant;
	next.timestamp_us = next.session = start + 95000;
	next.deadline_us = start + 195000;
	replaced.begin(start + 95000, c, next);
	expect(!replaced.recovering(), "a deliberate new session clears recovery");
	return passed;
}
