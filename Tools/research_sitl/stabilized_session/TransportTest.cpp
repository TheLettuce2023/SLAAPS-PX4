#include "Transport.hpp"
#include <cstdio>
#include <limits>
#include <string>

using namespace research_stabilized_session_fixture;
static unsigned checks{}, failures{};
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::fprintf(stderr, "line %u: %s\n", __LINE__, #value); } } while (0)

static bool same(const Command &a, const Command &b)
{
	bool equal = a.configuration == b.configuration && a.frame.session == b.frame.session
		     && a.frame.timestamp_us == b.frame.timestamp_us && a.frame.sequence == b.frame.sequence
		     && a.lease_us == b.lease_us && a.thrust_n == b.thrust_n;

	for (unsigned i = 0; i < 4; ++i) { equal &= a.frame.command[i] == b.frame.command[i]; }

	return equal;
}
static bool same(const Acknowledgement &a, const Acknowledgement &b)
{
	return a.configuration == b.configuration && a.value.session == b.value.session
	       && a.value.command_timestamp_us == b.value.command_timestamp_us && a.value.sequence == b.value.sequence
	       && a.value.accepted_us == b.value.accepted_us;
}

template<typename T>
static void rejects(const gz::msgs::Double_V &wire, const T &sentinel)
{
	T output = sentinel;
	CHECK(!decode(wire, output));
	CHECK(same(output, sentinel)); // Failure must never expose partially decoded commands.
}

template<typename T>
static void roundtrip(const T &input)
{
	gz::msgs::Double_V wire, received;
	CHECK(encode(input, wire));
	std::string bytes;
	CHECK(wire.SerializeToString(&bytes));
	CHECK(received.ParseFromString(bytes));
	T output{};
	CHECK(decode(received, output));
	CHECK(same(input, output));
}

int main()
{
	Command command{};
	command.configuration = 1;
	command.frame.session = 100;
	command.frame.timestamp_us = 110;
	command.frame.sequence = 1;
	command.lease_us = 20;
	command.thrust_n = 10.0F;
	command.frame.command = CanardVector{{-1.0F, -0.25F, 0.5F, 1.0F}};
	Acknowledgement ack{};
	ack.configuration = command.configuration;
	ack.value = {100, 110, 112, 1};
	roundtrip(command);
	roundtrip(ack);
	gz::msgs::Double_V valid_command, valid_ack;
	CHECK(encode(command, valid_command));
	CHECK(encode(ack, valid_ack));
	rejects(valid_ack, command);
	rejects(valid_command, ack);

	// Exact wire shape: legacy frames, truncated and extended payloads cannot
	// be interpreted as current command/ack records.
	for (unsigned size = 0; size <= 20; ++size) {
		for (unsigned kind = 0; kind < 2; ++kind) {
			auto wire = kind ? valid_ack : valid_command;

			while (wire.data_size() > static_cast<int>(size)) { wire.mutable_data()->RemoveLast(); }

			while (wire.data_size() < static_cast<int>(size)) { wire.add_data(0); }

			if (kind && size != kAcknowledgementFields) { rejects(wire, ack); }

			if (!kind && size != kCommandFields) { rejects(wire, command); }
		}
	}

	const double malformed[] = {std::numeric_limits<double>::quiet_NaN(),
				    std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()
				   };

	for (unsigned index = 0; index < kCommandFields; ++index) {
		for (double value : malformed) {
			auto wire = valid_command;
			wire.set_data(index, value);
			rejects(wire, command);
		}
	}

	for (unsigned index = 0; index < kAcknowledgementFields; ++index) {
		for (double value : malformed) {
			auto wire = valid_ack;
			wire.set_data(index, value);
			rejects(wire, ack);
		}
	}

	for (unsigned index = 0; index < 7; ++index) {
		for (double value : {-1.0, 0.5, static_cast<double>(kMaximumWireInteger) + 1.0}) {
			auto wire = valid_command;
			wire.set_data(index, value);
			rejects(wire, command);
			wire = valid_ack;
			wire.set_data(index, value);
			rejects(wire, ack);
		}
	}

	for (unsigned index : {0u, 1u, 2u, 3u, 5u, 6u}) {
		auto wire = valid_command;
		wire.set_data(index, 0);
		rejects(wire, command);
	}

	for (unsigned index : {0u, 1u, 2u, 3u, 5u}) {
		auto wire = valid_ack;
		wire.set_data(index, 0);
		rejects(wire, ack);
	}

	for (unsigned index = 8; index < 12; ++index) {
		for (double value : {-1.0001, 1.0001}) {
			auto wire = valid_command;
			wire.set_data(index, value);
			rejects(wire, command);
		}
	}

	for (double value : {-0.01, std::numeric_limits<double>::max()}) {
		auto wire = valid_command;
		wire.set_data(7, value);
		rejects(wire, command);
	}

	for (unsigned index : {2u, 4u, 6u}) {
		auto wire = valid_command;
		wire.set_data(index, index == 2 ? double{UINT32_MAX} + 1.0 : index == 4 ? 99.0 : static_cast<double>(kMaximumWireInteger));
		rejects(wire, command);
	}

	for (unsigned index : {2u, 4u, 6u}) {
		auto wire = valid_ack;
		wire.set_data(index, index == 2 ? double{UINT32_MAX} + 1.0 : 99.0);
		rejects(wire, ack);
	}

	// Preserve full sequence identity through real protobuf serialization,
	// including values above float32 precision and the maximum exact integer.
	for (uint64_t sequence : {uint64_t{1}, uint64_t{16777217}, kMaximumWireInteger}) {
		command.frame.sequence = sequence;
		ack.value.sequence = sequence;
		roundtrip(command);
		roundtrip(ack);
	}

	for (uint64_t bad : {kMaximumWireInteger + 1, UINT64_MAX}) {
		for (unsigned field = 0; field < 4; ++field) {
			auto candidate = command;
			uint64_t *fields[] = {&candidate.frame.session, &candidate.frame.timestamp_us, &candidate.frame.sequence, &candidate.lease_us};
			*fields[field] = bad;
			auto wire = valid_command;
			CHECK(!encode(candidate, wire));
			CHECK(wire.SerializeAsString() == valid_command.SerializeAsString());
			auto acknowledgement = ack;
			uint64_t *ack_fields[] = {&acknowledgement.value.session, &acknowledgement.value.command_timestamp_us,
						  &acknowledgement.value.sequence, &acknowledgement.value.accepted_us
						 };
			*ack_fields[field] = bad;
			wire = valid_ack;
			CHECK(!encode(acknowledgement, wire));
			CHECK(wire.SerializeAsString() == valid_ack.SerializeAsString());
		}
	}
	std::printf("Session transport: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
