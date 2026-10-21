#include "RemoteCompletion.hpp"
#include <cstdio>
using namespace research_stabilized_session_fixture;
namespace
{
unsigned checks{}, failures{};
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::fprintf(stderr, "line %u: %s\n", __LINE__, #value); } } while (0)
Command command {1, {100, 110, 1, {}}, 100, 10.0F};
gz::msgs::Double_V acknowledgement(TimeUs accepted = 115)
{
	gz::msgs::Double_V wire;
	CHECK(encode(Acknowledgement{1, {100, 110, accepted, 1}}, wire));
	return wire;
}
gz::msgs::Double_V disarmed(TimeUs handled = 140)
{
	gz::msgs::Double_V wire;

	for (uint64_t value : {uint64_t{1}, uint64_t{7}, uint64_t{1}, uint64_t{100}, handled}) { wire.add_data(static_cast<double>(value)); }
	return wire;
}
}
int main()
{
	{
		RemoteCompletion gate;
		CHECK(gate.poll(110, true, 7) == RemoteStatus::Idle);
		CHECK(gate.start(112, 42, command, 7, 50));
		CHECK(!gate.start(112, 43, command, 7, 50));
		CHECK(gate.poll(113, true, 7) == RemoteStatus::Waiting);
		CHECK(gate.acknowledge(114, true, 7, acknowledgement()).status == RemoteStatus::Waiting);
		CHECK(gate.pending());
		const auto result = gate.acknowledge(115, true, 7, acknowledgement());
		CHECK(result.status == RemoteStatus::Handled && result.ticket == 42 && result.write.handled && result.write.completed_us == 115);
		CHECK(!gate.pending());
		CHECK(gate.acknowledge(116, true, 7, acknowledgement()).status == RemoteStatus::Idle);
		gate.revoke(116); // No abandoned write: local driver still owns its separate barrier.
		CHECK(gate.start(117, 43, command, 7, 50));
	}

	for (unsigned fault = 0; fault < 9; ++fault) {
		RemoteCompletion gate;
		CHECK(gate.start(112, 42, command, 7, 50));
		auto wire = acknowledgement();

		if (fault == 0) { wire.set_data(2, 2); }

		if (fault == 1) { wire.set_data(3, 101); }

		if (fault == 2) { wire.set_data(4, 111); }

		if (fault == 3) { wire.set_data(5, 2); }

		if (fault == 4) { wire.set_data(6, 111); }

		const TimeUs now = fault == 5 ? 160 : fault == 6 ? 111 : 115;
		const auto result = gate.acknowledge(now, fault != 7, fault == 8 ? 8 : 7, wire);
		CHECK(result.status == RemoteStatus::Fault && !result.ticket && !result.write.handled);
		CHECK(gate.pending());
		CHECK(!gate.start(161, 43, command, 7, 50));
		CHECK(gate.confirmDisarm(170, disarmed(170)).status == RemoteStatus::Fault);
		gate.revoke(180);
		CHECK(gate.acknowledge(180, true, 7, acknowledgement()).status == RemoteStatus::Fault);
		CHECK(gate.confirmDisarm(180, disarmed(179)).status == RemoteStatus::Fault);
		CHECK(gate.confirmDisarm(180, disarmed(181)).status == RemoteStatus::Fault);
		auto wrong = disarmed(181); wrong.set_data(3, 101);
		CHECK(gate.confirmDisarm(181, wrong).status == RemoteStatus::Fault);
		CHECK(gate.pending());
		const auto stopped = gate.confirmDisarm(181, disarmed(181));
		CHECK(stopped.status == RemoteStatus::Disarmed && stopped.ticket == 42 && !gate.pending());
		CHECK(gate.confirmDisarm(181, disarmed(181)).status == RemoteStatus::Fault);
		auto next = command; next.frame.session = 182; next.frame.timestamp_us = 183;
		CHECK(gate.start(183, 43, next, 9, 50));
	}

	for (unsigned invalid = 0; invalid < 7; ++invalid) {
		RemoteCompletion gate;
		auto frame = command;

		if (invalid == 0) { frame.configuration = 0; }

		CHECK(!gate.start(invalid == 1 ? 109 : invalid == 2 ? 160 : 112,
				  invalid == 3 ? 0 : 42, frame, invalid == 4 ? 0 : 7, invalid == 5 ? 0 : invalid == 6 ? 100 : 50));
		CHECK(!gate.pending());
	}

	{
		RemoteCompletion gate;
		CHECK(gate.start(112, 42, command, 7, 50));
		CHECK(gate.acknowledge(159, true, 7, acknowledgement(159)).status == RemoteStatus::Handled);
	}

	{
		RemoteCompletion gate;
		CHECK(gate.start(112, 42, command, 7, 50));
		gate.revoke(120);
		auto wrong = disarmed(130); wrong.set_data(3, 101);
		CHECK(gate.confirmDisarm(140, wrong).status == RemoteStatus::Fault);
		CHECK(gate.confirmDisarm(139, disarmed(130)).status == RemoteStatus::Fault);
		CHECK(gate.pending());
		CHECK(gate.confirmDisarm(141, disarmed(130)).status == RemoteStatus::Disarmed);
	}

	std::printf("Remote completion: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
