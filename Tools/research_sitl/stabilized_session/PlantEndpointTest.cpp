#include "PlantEndpoint.hpp"
#include <cstdio>
#include <limits>

using namespace research_stabilized_session_fixture;
namespace
{
unsigned checks{}, failures{};
constexpr TimeUs kStart = 1000000;
constexpr TimeUs kTick = 4000;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::fprintf(stderr, "line %u: %s\n", __LINE__, #value); } } while (0)

OutputConfiguration configuration()
{
	OutputConfiguration result{};
	result.lease_us = 20000;
	result.acknowledgement_us = 8000;
	result.on_fault = OutputFallback::Disarmed;

	for (unsigned i = 0; i < 4; ++i) {
		result.servos[i] = {1000.0F, 1500.0F, 2000.0F, 1000, 1500, 2000};
		result.minimum[i] = -1.0F;
		result.maximum[i] = 1.0F;
	}

	return result;
}

struct Rig {
	OutputConfiguration config{configuration()};
	PlantEndpoint endpoint{1, config, 10.0F};
	StabilizedOutputProducer producer{};
	TimeUs now{kStart};
	TimeUs session{kStart};
	gz::msgs::Double_V wire{};
	OutputFrame frame{};
	PlantWrite write{};

	void disarm()
	{
		CHECK(endpoint.prepare(now, false, nullptr, write));
		CHECK(write.driver.candidate.disposition == OutputDisposition::Disarmed);
		CHECK(write.thrust_n == 0.0F);
		const auto result = endpoint.complete(write.driver.ticket, now, true, false);
		CHECK(result.handled && result.disarmed && !result.has_acknowledgement);
		CHECK(endpoint.ready());
	}
	void begin()
	{
		CHECK(!endpoint.grant(now, session, now + 100, 1, true));
		disarm();
		CHECK(!endpoint.grant(now, session, now, 1, true));
		CHECK(!endpoint.grant(now, session, now + 100, 2, true));
		CHECK(!endpoint.grant(now, now + 1, now + 100, 1, true));
		CHECK(endpoint.grant(now, session, now + 100, 1, true));
		CHECK(!endpoint.grant(now, session + 1, now + 100, 1, true));
		CHECK(producer.begin(now, session, config, true) == OutputStatus::Ok);
	}
	void send()
	{
		now += kTick;
		Report report{};
		report.timestamp_us = now;
		report.state = State::Rate;
		report.allocation.command = {-0.75F, -0.25F, 0.25F, 0.75F};
		CHECK(producer.submit(now, true, Status::Ok, report, frame) == OutputStatus::Ok);
		CHECK(encode(Command{1, frame, config.lease_us, 10.0F}, wire));
		// Real protobuf binary transport between producer and endpoint.
		gz::msgs::Double_V received;
		CHECK(received.ParseFromString(wire.SerializeAsString()));
		CHECK(endpoint.prepare(now, true, &received, write));
		CHECK(write.driver.candidate.disposition == OutputDisposition::Command);
		CHECK(write.thrust_n == 10.0F);
		CHECK(endpoint.pending(write.driver.ticket));
		CHECK(producer.poll(now, true) == OutputStatus::Waiting);

		for (unsigned i = 0; i < 4; ++i) { CHECK(write.driver.candidate.driver_control[i] == frame.command[i]); }
	}
	void accept()
	{
		const auto result = endpoint.complete(write.driver.ticket, now + 1, true, true);
		CHECK(result.handled && result.has_acknowledgement && !result.disarmed);
		Acknowledgement ack{};
		gz::msgs::Double_V received;
		CHECK(received.ParseFromString(result.acknowledgement.SerializeAsString()));
		CHECK(decode(received, ack));
		CHECK(ack.configuration == 1 && ack.value.accepted_us == now + 1);
		CHECK(producer.acknowledge(now + 1, true, ack.value) == OutputStatus::Ok);
		CHECK(!endpoint.complete(write.driver.ticket, now + 1, true, true).has_acknowledgement);
	}
};
}

int main()
{
	{
		Rig r;
		r.begin();

		for (unsigned cycle = 0; cycle < 10; ++cycle) { r.send(); r.accept(); }

		// Held output does not acknowledge the old frame again or refresh lease.
		CHECK(r.endpoint.prepare(r.now + 2, true, nullptr, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Command);
		CHECK(!r.endpoint.complete(r.write.driver.ticket, r.now + 2, true, true).has_acknowledgement);
		r.now += r.config.lease_us;
		CHECK(r.endpoint.prepare(r.now, true, nullptr, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Disarmed && r.write.thrust_n == 0.0F);
		CHECK(r.endpoint.faulted());
		CHECK(!r.endpoint.complete(r.write.driver.ticket, r.now, true, true).has_acknowledgement);
		CHECK(!r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
		r.endpoint.revoke(); r.disarm();
		CHECK(r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
	}

	// Completion must be matching, timely, handled, still permitted and not revoked.
	for (unsigned fault = 0; fault < 7; ++fault) {
		Rig r; r.begin(); r.send();

		if (fault == 0) { r.endpoint.revoke(); CHECK(!r.endpoint.pending(r.write.driver.ticket)); }

		if (fault == 1) { CHECK(r.endpoint.cancel(r.write.driver.ticket)); }

		if (fault == 2) {
			CHECK(!r.endpoint.complete(r.write.driver.ticket + 1, r.now, true, true).has_acknowledgement);
			CHECK(r.endpoint.pending(r.write.driver.ticket));
			r.endpoint.revoke();
		}

		const TimeUs completed = fault == 3 ? r.now + r.config.acknowledgement_us : fault == 4 ? r.now - 1 : r.now;
		const auto result = r.endpoint.complete(r.write.driver.ticket, completed, fault != 5, fault != 6);
		CHECK(!result.has_acknowledgement);
		r.now += r.config.lease_us;
		r.endpoint.revoke(); r.disarm();
		CHECK(r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
	}

	// Malformed, wrong-profile, wrong-lease, wrong-thrust, duplicate, future and
	// skipped-sequence traffic all inhibit output and require explicit recovery.
	for (unsigned fault = 0; fault < 8; ++fault) {
		Rig r; r.begin(); r.send(); r.accept();
		auto bad = r.wire;

		if (fault == 0) { bad.set_data(0, std::numeric_limits<double>::quiet_NaN()); }

		if (fault == 1) { bad.set_data(2, 2); }

		if (fault == 2) { bad.set_data(6, r.config.lease_us + 1); }

		if (fault == 3) { bad.set_data(7, 11); }

		if (fault == 5) { bad.set_data(4, static_cast<double>(r.now + 10000)); bad.set_data(5, 2); }

		if (fault == 6) { bad.set_data(4, static_cast<double>(r.now + 1)); bad.set_data(5, 3); }

		if (fault == 7) { bad.set_data(3, static_cast<double>(r.session + 1)); }

		r.now += kTick;
		CHECK(r.endpoint.prepare(r.now, true, &bad, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Disarmed && r.write.thrust_n == 0.0F);
		CHECK(!r.endpoint.complete(r.write.driver.ticket, r.now, true, true).has_acknowledgement);
		CHECK(r.endpoint.faulted());
		CHECK(!r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
		CHECK(r.endpoint.prepare(r.now + 1, true, nullptr, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Disarmed);
		CHECK(!r.endpoint.complete(r.write.driver.ticket, r.now + 1, true, true).has_acknowledgement);
		r.now += kTick;
		r.endpoint.revoke(); r.disarm();
		CHECK(r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
	}

	{
		Rig r; r.begin(); r.send(); r.accept();
		r.now += kTick;
		CHECK(r.endpoint.prepare(r.now, false, nullptr, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Disarmed);
		CHECK(!r.endpoint.complete(r.write.driver.ticket, r.now, true, false).has_acknowledgement);
		CHECK(r.endpoint.faulted());
	}

	{
		Rig r;
		Command command{1, {kStart, kStart + kTick, 1, {0.0F, 0.0F, 0.0F, 0.0F}}, r.config.lease_us, 10.0F};
		CHECK(encode(command, r.wire));
		CHECK(r.endpoint.prepare(kStart + kTick, true, &r.wire, r.write));
		CHECK(r.write.driver.candidate.disposition == OutputDisposition::Disarmed);
		CHECK(!r.endpoint.complete(r.write.driver.ticket, kStart + kTick, true, true).has_acknowledgement);
		CHECK(r.endpoint.faulted() && !r.endpoint.ready());
		CHECK(!r.endpoint.grant(kStart + kTick, kStart, kStart + 10000, 1, true));
		r.now += 2 * kTick;
		r.endpoint.revoke(); r.disarm();
		CHECK(r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
	}

	{
		Rig r; r.begin(); r.send();
		const uint64_t ticket = r.write.driver.ticket;
		PlantWrite competing{};
		CHECK(!r.endpoint.prepare(r.now, true, &r.wire, competing));
		CHECK(competing.driver.ticket == 0);
		CHECK(r.endpoint.pending(ticket));
		CHECK(!r.endpoint.grant(r.now, r.now, r.now + 100, 1, true));
		r.accept();
		r.endpoint.revoke();
		CHECK(!r.endpoint.grant(r.now + 1, r.now + 1, r.now + 100, 1, true));
		r.now += kTick; r.disarm();
		// Old session replay cannot survive revoke/disarm.
		CHECK(!r.endpoint.grant(r.now, r.session, r.now + 100, 1, true));
		CHECK(r.endpoint.faulted());
	}

	for (unsigned invalid = 0; invalid < 4; ++invalid) {
		auto config = configuration();

		if (invalid == 0) { config.on_fault = OutputFallback::Calibrated; }

		if (invalid == 1) { config.lease_us = 0; }

		PlantEndpoint endpoint(invalid == 2 ? 0 : 1, config,
				       invalid == 3 ? std::numeric_limits<float>::quiet_NaN() : 10.0F);
		PlantWrite write{};
		CHECK(endpoint.prepare(kStart, false, nullptr, write));
		CHECK(write.driver.candidate.disposition == OutputDisposition::Disarmed);
		CHECK(endpoint.complete(write.driver.ticket, kStart, true, false).disarmed);
		CHECK(!endpoint.ready());
		CHECK(!endpoint.grant(kStart, kStart, kStart + 100, 1, true));
	}

	std::printf("Plant endpoint: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
