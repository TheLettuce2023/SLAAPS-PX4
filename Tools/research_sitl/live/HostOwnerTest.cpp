#include "HostOwner.hpp"
#include "SignedMavlink.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/random.h>

namespace
{
constexpr uint32_t kDisarmed = C1H_SIM_SELECTED | C1H_SIM_CONNECTED | C1H_SIM_REQUIRED_VALID | C1H_SIM_MAP_VALID;
constexpr uint32_t kArmed = kDisarmed | C1H_SIM_ARMED;
constexpr uint64_t kTestDurationUs = 5000000;
constexpr uint64_t kReceiptAgeUs = 1000000;
constexpr uint64_t kValidityUs = 80000;
unsigned checks = 0;

void require(bool condition, const char *label)
{
	++checks;

	if (!condition) { throw std::runtime_error(label); }
}

uint64_t nowUs() { return c1h_now_ns() / 1000; }

struct Fixture {
	c1h_config config{};
	c1h_context *context{nullptr};
	mavlink_signing_t signing{};
	mavlink_status_t encoder{};
	c1h_result result{};

	Fixture()
	{
		result.abi_version = C1H_ABI_VERSION;
		result.struct_size = sizeof(result);
		config.abi_version = C1H_ABI_VERSION;
		config.struct_size = sizeof(config);
		require(getrandom(config.key, sizeof(config.key), 0) == static_cast<ssize_t>(sizeof(config.key)), "fresh fixture key");
		// These are isolated ABI-test identities, never live deployment values.
		config.run_id = 1;
		config.source_id = 2;
		config.session_id = 3;
		config.controller_id = 4;
		config.channel_id = 5;
		config.map_epoch = 6;
		config.map_start_us = nowUs();
		config.map_end_us = config.map_start_us + kTestDurationUs;
		config.signing_seed = config.map_start_us / 10;
		config.maximum_receipt_age_us = kReceiptAgeUs;
		config.maximum_confirmation_age_us = kReceiptAgeUs;
		require(c1h_create(&config, &context) == C1H_OK && context != nullptr, "create");
		std::memcpy(signing.secret_key, config.key, sizeof(config.key));
		signing.timestamp = config.signing_seed;
		signing.link_id = 7;
		signing.flags = MAVLINK_SIGNING_FLAG_SIGN_OUTGOING;
		encoder.signing = &signing;
		require(c1h_tick(context, kDisarmed, &result) == C1H_OK && result.report_count == 2,
			"initial real poll reports");
	}

	~Fixture()
	{
		c1h_destroy(context);
		volatile uint8_t *first = config.key;
		volatile uint8_t *second = signing.secret_key;

		for (unsigned index = 0; index < sizeof(config.key); ++index) { first[index] = 0; second[index] = 0; }
	}

	void receive(const mavlink_message_t &message)
	{
		uint8_t bytes[MAVLINK_MAX_PACKET_LEN] {};
		const auto length = mavlink_msg_to_send_buffer(bytes, &message);
		require(c1h_receive(context, bytes, length, nowUs(), config.channel_id, &result) == C1H_OK,
			"native receive returns");
		require(result.parser_reason == 0 && result.signature_verified == 1, "actual generated authentication");
	}

	void session(uint8_t operation, uint64_t operation_sequence, uint64_t acknowledged = 0, uint64_t created = 0)
	{
		mavlink_research_acceleration_session_t value{};
		value.protocol_version = research_acceleration::kWireProtocolVersionV1;
		value.source_id = config.source_id;
		value.session_id = config.session_id;
		value.controller_instance_id = config.controller_id;
		value.transition_epoch = result.transition_epoch;
		value.clock_mapping_epoch = config.map_epoch;
		value.target_system = 1;
		value.target_component = 1;
		value.operation = operation;
		value.operation_sequence = operation_sequence;
		value.acknowledged_status_sequence = acknowledged;
		value.producer_acknowledgement_monotonic_us = created;
		mavlink_message_t message{};
		mavlink_msg_research_acceleration_session_encode_status(42, 191, &encoder, &message, &value);
		receive(message);
	}

	void request(uint64_t sequence, uint64_t created)
	{
		mavlink_research_acceleration_request_t value{};
		value.protocol_version = research_acceleration::kWireProtocolVersionV1;
		value.source_id = config.source_id;
		value.session_id = config.session_id;
		value.controller_instance_id = config.controller_id;
		value.transition_epoch = result.transition_epoch;
		value.clock_mapping_epoch = config.map_epoch;
		value.target_system = 1;
		value.target_component = 1;
		value.sequence = sequence;
		value.producer_monotonic_us = created;
		value.producer_expiry_monotonic_us = created + kValidityUs;
		value.frame = 1;
		value.quantity = 1;
		value.reference_point = 1;
		value.acceleration_x_frd_m_s2 = 1.F;
		value.acceleration_y_frd_m_s2 = -2.F;
		value.acceleration_z_frd_m_s2 = 3.F;
		mavlink_message_t message{};
		mavlink_msg_research_acceleration_request_encode_status(42, 191, &encoder, &message, &value);
		receive(message);
	}

	void authorize(uint32_t intent)
	{
		const bool clear = intent == 2;
		require(c1h_operator_prepare(context, intent, clear ? 0 : config.source_id,
					     clear ? 0 : config.session_id, &result) == C1H_OK && result.ticket != 0, "independent prepare");
		const uint64_t ticket = result.ticket;
		require(c1h_operator_confirm(context, ticket, &result) == C1H_OK
			&& result.event_consumed && result.has_adapter_code && result.adapter_code == 1,
			"independent confirmed actual administration");
	}
};

uint64_t strictlyLater(uint64_t lower)
{
	const uint64_t begin = c1h_now_ns();

	while (nowUs() <= lower) {
		require(c1h_now_ns() - begin < 100000000, "bounded clock ordering wait");
	}

	return nowUs();
}

void lifecycle()
{
	Fixture fixture;
	const uint64_t initial_epoch = fixture.result.transition_epoch;
	fixture.session(1, 0);
	require(fixture.result.receipt_offered == 1 && fixture.result.transition_epoch == initial_epoch,
		"guidance establishment alone cannot authorize");
	require(c1h_access(fixture.context, &fixture.result) == C1H_OK && !fixture.result.has_eligible_request,
		"no authority before independent consent");
	require(c1h_operator_attach(fixture.context, 1) == C1H_OK, "operator attach");
	fixture.authorize(0);
	require(c1h_tick(fixture.context, kArmed, &fixture.result) == C1H_OK
		&& fixture.result.state_generation == 2 && fixture.result.admission_closed,
		"simulated arm transition closes acknowledgement");
	const uint64_t barrier = fixture.result.barrier_report_sequence;
	const uint64_t ack_time = strictlyLater(fixture.result.barrier_time_us + 1);
	fixture.session(3, 1, barrier, ack_time);
	require(fixture.result.code_category == 2 && fixture.result.native_code == 1
		&& !fixture.result.admission_closed, "actual ACK accepted");
	const uint64_t created = strictlyLater(ack_time + 3);
	fixture.request(1, created);
	require(fixture.result.code_category == 2 && fixture.result.native_code == 1, "actual request accepted");
	require(c1h_access(fixture.context, &fixture.result) == C1H_OK && fixture.result.has_eligible_request
		&& fixture.result.request_sequence == 1 && fixture.result.request_created_us == created
		&& fixture.result.acceleration_frd_m_s2[0] == 1.F
		&& fixture.result.acceleration_frd_m_s2[1] == -2.F
		&& fixture.result.acceleration_frd_m_s2[2] == 3.F, "actual request copied only through access");
	const uint64_t generation = fixture.result.state_generation;
	require(c1h_tick(fixture.context, kArmed, &fixture.result) == C1H_OK
		&& fixture.result.state_generation == generation, "clock-only tick preserves generation");
	// Real signed fault injection: still newer than ACK but below accepted producer time.
	const uint64_t later = strictlyLater(created + 10);
	fixture.request(2, later);
	require(fixture.result.native_code == 1, "second request accepted before regression");
	fixture.request(3, created);
	require(fixture.result.native_code == static_cast<uint32_t>(research_acceleration::IngressCode::S1Rejected)
		&& fixture.result.admission_closed, "actual producer regression latches and closes ACK");
	require(c1h_tick(fixture.context, kDisarmed, &fixture.result) == C1H_OK, "simulated disarm before clear");
	fixture.authorize(2);
	require(c1h_access(fixture.context, &fixture.result) == C1H_OK && !fixture.result.has_eligible_request
		&& fixture.result.admission_closed, "clear never restores eligibility");
	require(c1h_operator_detach(fixture.context) == C1H_OK, "operator detach");
}

void argumentsAndClock()
{
	require(c1h_config_size() == sizeof(c1h_config) && c1h_result_size() == sizeof(c1h_result), "C ABI size");
	c1h_context *context = nullptr;
	c1h_config invalid{};
	require(c1h_create(nullptr, &context) == C1H_INVALID_ARGUMENT && context == nullptr, "null configuration");
	require(c1h_create(&invalid, &context) == C1H_INVALID_CONFIG && context == nullptr, "invalid configuration");
	c1h_result output{};
	output.abi_version = C1H_ABI_VERSION;
	output.struct_size = sizeof(output);
	require(c1h_tick(nullptr, kDisarmed, &output) == C1H_INVALID_ARGUMENT, "null context");
	require(c1h_access(nullptr, nullptr) == C1H_INVALID_ARGUMENT, "null output");
	Fixture fixture;
	const uint64_t before = c1h_now_ns();
	require(c1h_tick(fixture.context, kDisarmed, &output) == C1H_OK, "clock sample tick");
	const uint64_t after = c1h_now_ns();
	require(output.now_us >= before / 1000 && output.completion_us <= after / 1000
		&& output.completion_us >= output.now_us, "native sample and completion bracketed");
	require(c1h_tick(fixture.context, 32, &output) == C1H_INVALID_ARGUMENT, "invalid simulated flags");
	require(c1h_tick(fixture.context, kDisarmed, &output) == C1H_OK && output.state_generation == 1,
		"invalid flags did not alter trusted state");
}

void resultHeaderCanaries()
{
	Fixture fixture;

	for (unsigned invalid = 0; invalid < 3; ++invalid) {
		for (unsigned operation = 0; operation < 5; ++operation) {
			alignas(c1h_result) uint8_t storage[sizeof(c1h_result) + 16] {};
			std::memset(storage, 0xa5, sizeof(storage));
			const uint32_t header[2] {static_cast<uint32_t>(C1H_ABI_VERSION) + (invalid == 0 ? 1U : 0U),
						  invalid == 1 ? 8U : static_cast<uint32_t>(sizeof(c1h_result) + (invalid == 2 ? 8U : 0U))
						 };
			std::memcpy(storage, header, sizeof(header));
			uint8_t before[sizeof(storage)] {};
			std::memcpy(before, storage, sizeof(storage));
			auto *output = reinterpret_cast<c1h_result *>(storage);
			int result = C1H_OK;

			switch (operation) {
			case 0: result = c1h_tick(fixture.context, kArmed, output); break;

			case 1: result = c1h_receive(fixture.context, nullptr, 0, 0, 0, output); break;

			case 2: result = c1h_access(fixture.context, output); break;

			case 3: result = c1h_operator_prepare(fixture.context, 0, 2, 3, output); break;

			case 4: result = c1h_operator_confirm(fixture.context, 1, output); break;
			}

			require(result == C1H_INVALID_ARGUMENT, "invalid result ABI header accepted");
			require(std::memcmp(before, storage, sizeof(storage)) == 0,
				"invalid result ABI overwrote header or trailing canary");
		}
	}

	require(c1h_tick(fixture.context, kDisarmed, &fixture.result) == C1H_OK
		&& fixture.result.state_generation == 1, "invalid result headers mutated owner state");
}
}

int main()
{
	try {
		argumentsAndClock();
		resultHeaderCanaries();
		lifecycle();
		std::cout << checks << " host owner checks passed\n";
		return 0;

	} catch (const std::exception &error) {
		std::cerr << "HostOwnerTest failed: " << error.what() << '\n';
		return 1;
	}
}
