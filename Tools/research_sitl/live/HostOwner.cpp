#include "HostOwner.hpp"
#include "SignedMavlink.hpp"
#include "../supervisor/HarnessSupervisorOwner.hpp"

#include <cstring>
#include <limits>
#include <new>
#include <time.h>

using namespace research_acceleration;
using namespace research_sitl;

namespace
{
constexpr uint64_t kNanosecondsPerSecond = 1000000000;
constexpr uint64_t kNanosecondsPerMicrosecond = 1000;
constexpr uint64_t kSigningUnitUs = 10;
constexpr uint64_t kMaximumSigningTime = (uint64_t{1} << 48) - 1;
constexpr uint64_t kValidityUs = 80000;
constexpr uint64_t kHardAgeUs = 60000;
constexpr uint64_t kFutureSkewUs = 8000;
constexpr uint64_t kProfileUncertaintyCeilingUs = 4000;
constexpr uint64_t kIdentityMapUncertaintyUs = 1;
constexpr uint32_t kStateMask = C1H_SIM_ARMED | C1H_SIM_SELECTED | C1H_SIM_CONNECTED
				| C1H_SIM_REQUIRED_VALID | C1H_SIM_MAP_VALID;
constexpr uint32_t kInitialState = kStateMask & ~C1H_SIM_ARMED;
constexpr uint8_t kAcknowledgeOperation = 3;
constexpr uint8_t kEstablishOperation = 1;
constexpr uint8_t kRevokeOperation = 2;

bool readClock(uint64_t &nanoseconds)
{
	timespec value{};

	if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0
	    || static_cast<uint64_t>(value.tv_nsec) >= kNanosecondsPerSecond
	    || static_cast<uint64_t>(value.tv_sec) > (UINT64_MAX - static_cast<uint64_t>(value.tv_nsec)) / kNanosecondsPerSecond) {
		return false;
	}

	nanoseconds = static_cast<uint64_t>(value.tv_sec) * kNanosecondsPerSecond + static_cast<uint64_t>(value.tv_nsec);
	return nanoseconds != 0;
}

OwnerHarnessConfig ownerConfiguration(const c1h_config &config)
{
	// Experimental synthetic bounds only, copied from the approved host profile.
	const ContractConfig contract{kWireProtocolVersionV1, config.source_id, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{kValidityUs, kValidityUs, kHardAgeUs, kFutureSkewUs, kProfileUncertaintyCeilingUs}, 0.01F};
	return {contract, {config.controller_id}, config.run_id, config.maximum_receipt_age_us};
}

SignedMavlinkConfig parserConfiguration(const c1h_config &config)
{
	SignedMavlinkConfig parser{};
	std::memcpy(parser.key, config.key, sizeof(parser.key));
	parser.source = config.source_id;
	parser.session = config.session_id;
	parser.controller_instance = config.controller_id;
	parser.signing_seed = config.signing_seed;
	return parser;
}

bool validConfiguration(const c1h_config &config, uint64_t now)
{
	if (config.abi_version != C1H_ABI_VERSION || config.struct_size != sizeof(c1h_config)) { return false; }

	bool has_key = false;

	for (uint8_t byte : config.key) { has_key = has_key || byte != 0; }

	return has_key && config.run_id != 0 && config.source_id != 0 && config.session_id != 0
	       && config.controller_id != 0 && config.channel_id != 0 && config.map_epoch != 0
	       && config.signing_seed != 0 && config.signing_seed < kMaximumSigningTime
	       && config.map_start_us <= now && config.map_end_us > now
	       && config.map_end_us - now >= kValidityUs && config.maximum_receipt_age_us != 0
	       && config.maximum_confirmation_age_us != 0;
}
}

struct c1h_context {
	c1h_config configuration;
	HarnessSupervisorOwner owner;
	SupervisorBroker broker;
	SupervisorConsoleFacade console;
	SignedMavlink parser;
	uint64_t signing_origin_us;
	uint64_t last_time_us{0};
	uint64_t observation{0};
	uint64_t receipt{0};
	uint64_t generation{1};
	uint32_t state_flags{kInitialState};
	uint32_t observed_flags{kInitialState};
	bool observed{false};
	bool failed{false};
	bool in_call{false};
	AdapterSnapshot snapshot{};

	explicit c1h_context(const c1h_config &config, uint64_t now) :
		configuration(config), owner(ownerConfiguration(config)),
		broker({config.maximum_confirmation_age_us}, owner), console(broker),
	       parser(parserConfiguration(config)), signing_origin_us(now)
	{
		// The parser owns the retained key; the generic configuration does not.
		volatile uint8_t *key = configuration.key;

		for (unsigned index = 0; index < sizeof(configuration.key); ++index) { key[index] = 0; }
	}
};

namespace
{
void initialize(c1h_result &output)
{
	output = {};
	output.abi_version = C1H_ABI_VERSION;
	output.struct_size = sizeof(output);
	output.parser_reason = static_cast<uint32_t>(SignedParseReason::NotConfigured);
}

int fail(c1h_context &context, c1h_result &output, uint32_t code)
{
	context.failed = true;
	output.result_code = code;
	output.has_eligible_request = 0;
	return static_cast<int>(code);
}

class CallScope
{
public:
	explicit CallScope(c1h_context &context) : context_(context) { context_.in_call = true; }
	~CallScope() { context_.in_call = false; }
private:
	c1h_context &context_;
};

int available(c1h_context *context, c1h_result *output)
{
	if (output == nullptr) { return C1H_INVALID_ARGUMENT; }

	// Only the fixed ABI header is readable until its advertised extent/version
	// is validated. Rejected buffers remain byte-for-byte unchanged.
	uint32_t header[2] {};
	std::memcpy(header, output, sizeof(header));

	if (header[0] != C1H_ABI_VERSION || header[1] != sizeof(c1h_result)) { return C1H_INVALID_ARGUMENT; }

	initialize(*output);

	if (context == nullptr) {
		output->result_code = C1H_INVALID_ARGUMENT;
		return C1H_INVALID_ARGUMENT;
	}

	if (context->failed || context->in_call) {
		fail(*context, *output, C1H_OWNER_UNAVAILABLE);
		return C1H_OWNER_UNAVAILABLE;
	}

	return C1H_OK;
}

void captureSnapshot(c1h_context &context, const AdapterSnapshot &snapshot, c1h_result &output)
{
	context.snapshot = snapshot;
	output.state_generation = snapshot.state_generation;
	output.transition_epoch = snapshot.transition_epoch;
	output.barrier_report_sequence = snapshot.barrier_report_sequence;
	output.barrier_time_us = snapshot.barrier_validator_time_us;
	output.last_report_sequence = snapshot.last_report_sequence;
	output.terminal = snapshot.terminal;
}

template<typename Report>
bool appendReport(c1h_context &context, const Report &report, c1h_result &output)
{
	if (output.report_count >= C1H_MAX_REPORTS) { fail(context, output, C1H_STATUS_FAILURE); return false; }

	WireStatusV1 status{};
	c1h_status encoded{};

	if (buildStatusV1(report, {42, 191, StatusDelivery::Directed}, status) != StatusBuildCode::Ok
	    || encodeStatusV1Payload(status, encoded.payload) != StatusEncodeCode::Ok
	    || output.now_us < context.signing_origin_us) {
		fail(context, output, C1H_STATUS_FAILURE);
		return false;
	}

	const uint64_t elapsed = (output.now_us - context.signing_origin_us) / kSigningUnitUs;

	if (elapsed > kMaximumSigningTime - context.configuration.signing_seed) {
		fail(context, output, C1H_STATUS_FAILURE);
		return false;
	}

	size_t length = 0;

	if (!context.parser.signStatus(status, context.configuration.signing_seed + elapsed,
				       encoded.frame, sizeof(encoded.frame), length)) {
		fail(context, output, C1H_STATUS_FAILURE);
		return false;
	}

	encoded.report_sequence = status.status_sequence;
	encoded.transition_epoch = status.transition_epoch;
	encoded.validator_time_us = status.validator_time_us;
	encoded.frame_length = static_cast<uint32_t>(length);
	encoded.status_class = status.status_class;
	encoded.decision_code = status.decision_code;
	encoded.status_flags = status.status_flags;
	output.reports[output.report_count++] = encoded;
	return true;
}

template<typename Result>
bool appendResult(c1h_context &context, const Result &result, uint32_t category, c1h_result &output)
{
	captureSnapshot(context, result.snapshot, output);
	output.code_category = category;
	output.native_code = static_cast<uint32_t>(result.code);
	output.admission_closed = result.admission_closed;

	for (uint8_t index = 0; index < result.report_count; ++index) {
		if (!appendReport(context, result.reports[index], output)) { return false; }
	}

	return true;
}

bool sampleAndTick(c1h_context &context, c1h_result &output)
{
	uint64_t nanoseconds = 0;

	if (!readClock(nanoseconds)) { fail(context, output, C1H_CLOCK_FAULT); return false; }

	const uint64_t now = nanoseconds / kNanosecondsPerMicrosecond;
	output.now_us = now;

	if ((context.observed && now < context.last_time_us) || context.observation == UINT64_MAX) {
		fail(context, output, C1H_CLOCK_FAULT);
		return false;
	}

	uint32_t effective_flags = context.state_flags;

	if (now < context.configuration.map_start_us || now > context.configuration.map_end_us) {
		effective_flags &= ~C1H_SIM_MAP_VALID;
	}

	if (context.observed && effective_flags != context.observed_flags) {
		if (context.generation == UINT64_MAX) { fail(context, output, C1H_OWNER_UNAVAILABLE); return false; }

		++context.generation;
	}

	context.observed_flags = effective_flags;
	context.observed = true;
	context.last_time_us = now;
	TrustedStateSample sample{};
	sample.generation = context.generation;
	sample.observer_continuous = true;
	sample.request_guard_context = {now,
					(effective_flags & C1H_SIM_ARMED) != 0 ? ArmState::Armed : ArmState::Disarmed,
					(effective_flags & C1H_SIM_SELECTED) != 0 ? ResearchModeState::Selected : ResearchModeState::NotSelected,
					(effective_flags & C1H_SIM_MAP_VALID) != 0, (effective_flags & C1H_SIM_CONNECTED) != 0,
					(effective_flags & C1H_SIM_REQUIRED_VALID) != 0
				       };
	sample.map = {(effective_flags & C1H_SIM_MAP_VALID) != 0,
		      context.configuration.source_id, context.configuration.session_id, context.configuration.controller_id,
		      context.configuration.map_epoch, 0, kIdentityMapUncertaintyUs,
		      context.configuration.map_start_us, context.configuration.map_end_us
		     };
	const FixtureStateReceipt receipt{sample,
		{context.configuration.run_id, ++context.observation, now, true}, context.configuration.run_id};
	AdapterResult result{};
	const bool valid = context.owner.tick(receipt, &result);

	if (result.report_count != 0 && !appendResult(context, result, 1, output)) { return false; }

	if (!valid || result.snapshot.terminal || !result.snapshot.initialized) {
		fail(context, output, C1H_OWNER_UNAVAILABLE);
		return false;
	}

	return true;
}

int finish(c1h_context &context, c1h_result &output)
{
	uint64_t nanoseconds = 0;

	if (!readClock(nanoseconds) || nanoseconds / kNanosecondsPerMicrosecond < output.now_us) {
		return fail(context, output, C1H_CLOCK_FAULT);
	}

	output.completion_us = nanoseconds / kNanosecondsPerMicrosecond;
	return static_cast<int>(output.result_code);
}

void captureConsole(const ConsoleResult &result, c1h_result &output)
{
	output.broker_code = static_cast<uint32_t>(result.code);
	output.console_disposition = static_cast<uint32_t>(result.disposition);
	output.ticket = result.ticket;
	output.has_adapter_code = result.has_adapter_code;
	output.adapter_code = static_cast<uint32_t>(result.adapter_code);
	output.event_consumed = result.event_consumed;
}
}

uint32_t c1h_config_size(void) { return sizeof(c1h_config); }
uint32_t c1h_result_size(void) { return sizeof(c1h_result); }

uint64_t c1h_now_ns(void)
{
	uint64_t value = 0;
	return readClock(value) ? value : 0;
}

int c1h_create(const c1h_config *config, c1h_context **output)
{
	if (output == nullptr) { return C1H_INVALID_ARGUMENT; }

	*output = nullptr;

	if (config == nullptr) { return C1H_INVALID_ARGUMENT; }

	uint64_t nanoseconds = 0;

	if (!readClock(nanoseconds)) { return C1H_CLOCK_FAULT; }

	const uint64_t now = nanoseconds / kNanosecondsPerMicrosecond;

	if (!validConfiguration(*config, now)) { return C1H_INVALID_CONFIG; }

	c1h_context *created = nullptr;

	try {
		created = new (std::nothrow) c1h_context(*config, now);

	} catch (...) {
		return C1H_RESOURCE_FAILURE;
	}

	if (created == nullptr) { return C1H_RESOURCE_FAILURE; }

	if (!created->parser.configured()) {
		delete created;
		return C1H_INVALID_CONFIG;
	}

	*output = created;
	return C1H_OK;
}

void c1h_destroy(c1h_context *context)
{
	if (context != nullptr && context->in_call) { context->failed = true; return; }

	delete context;
}

int c1h_tick(c1h_context *context, uint32_t simulated_state_flags, c1h_result *output)
{
	const int availability = available(context, output);

	if (availability != C1H_OK) { return availability; }

	CallScope scope(*context);

	if ((simulated_state_flags & ~kStateMask) != 0) {
		output->result_code = C1H_INVALID_ARGUMENT;
		return finish(*context, *output);
	}

	context->state_flags = simulated_state_flags;
	sampleAndTick(*context, *output);
	return finish(*context, *output);
}

int c1h_receive(c1h_context *context, const uint8_t *bytes, uint32_t length,
		uint64_t received_us, uint64_t channel_id, c1h_result *output)
{
	const int availability = available(context, output);

	if (availability != C1H_OK) { return availability; }

	CallScope scope(*context);
	output->received_us = received_us;

	if (bytes == nullptr || length == 0 || received_us == 0 || channel_id == 0) {
		output->result_code = C1H_INVALID_ARGUMENT;
		return finish(*context, *output);
	}

	if (!sampleAndTick(*context, *output)) { return finish(*context, *output); }

	if (received_us > output->now_us) {
		output->result_code = C1H_INVALID_ARGUMENT;
		return finish(*context, *output);
	}

	const SignedParseResult parsed = context->parser.authenticate(bytes, length,
					 channel_id == context->configuration.channel_id,
					 context->snapshot.transition_epoch, context->configuration.map_epoch, true);
	output->parser_reason = static_cast<uint32_t>(parsed.reason);
	output->packet_kind = static_cast<uint32_t>(parsed.kind);
	output->signature_verified = parsed.signature_verified;

	if (parsed.reason != SignedParseReason::Ok) { return finish(*context, *output); }

	// Authentication consumes time. Re-sample before any actual owner dispatch.
	if (!sampleAndTick(*context, *output)) {
		context->parser.discardPending();
		return finish(*context, *output);
	}

	if ((context->observed_flags & C1H_SIM_MAP_VALID) == 0
	    || !context->parser.commitPending(context->snapshot.transition_epoch, context->configuration.map_epoch)) {
		context->parser.discardPending();
		output->parser_reason = static_cast<uint32_t>(SignedParseReason::ContextBinding);
		output->packet_kind = static_cast<uint32_t>(SignedPacketKind::None);
		return finish(*context, *output);
	}

	if (parsed.kind == SignedPacketKind::Request) {
		IngressResult result{};

		if (!context->owner.processRequest(parsed.request, parsed.verdict, result)) {
			fail(*context, *output, C1H_OWNER_UNAVAILABLE);

		} else {
			appendResult(*context, result, 2, *output);
		}

	} else if (parsed.kind == SignedPacketKind::Session && parsed.session.operation == kAcknowledgeOperation) {
		IngressResult result{};

		if (!context->owner.processAcknowledgement(parsed.session, parsed.verdict, result)) {
			fail(*context, *output, C1H_OWNER_UNAVAILABLE);

		} else {
			appendResult(*context, result, 2, *output);
		}

	} else if (parsed.kind == SignedPacketKind::Session
		   && (parsed.session.operation == kEstablishOperation || parsed.session.operation == kRevokeOperation)) {
		if (context->receipt == UINT64_MAX) {
			fail(*context, *output, C1H_OWNER_UNAVAILABLE);

		} else {
			const ImmutableIngressReceipt receipt{context->configuration.run_id, ++context->receipt,
							      received_us, parsed.session, parsed.verdict};
			output->receipt_offered = context->owner.offer(receipt);
		}

	} else {
		output->result_code = C1H_INVALID_ARGUMENT;
	}

	return finish(*context, *output);
}

int c1h_access(c1h_context *context, c1h_result *output)
{
	const int availability = available(context, output);

	if (availability != C1H_OK) { return availability; }

	CallScope scope(*context);

	if (sampleAndTick(*context, *output)) {
		CanonicalRequest request{};
		AdapterResult result{};

		if (!context->owner.getEligibleRequest(request, result)) {
			fail(*context, *output, C1H_OWNER_UNAVAILABLE);

		} else if (appendResult(*context, result, 1, *output) && result.code == AdapterCode::EligibleRequest) {
			output->has_eligible_request = 1;
			output->request_sequence = request.sequence;
			output->request_created_us = request.producer_monotonic_us;
			output->request_expiry_us = request.producer_expiry_monotonic_us;

			for (unsigned index = 0; index < 3; ++index) { output->acceleration_frd_m_s2[index] = request.acceleration_frd_m_s2(index); }
		}
	}

	return finish(*context, *output);
}

int c1h_operator_attach(c1h_context *context, uint64_t operator_instance)
{
	if (context == nullptr || operator_instance == 0) { return C1H_INVALID_ARGUMENT; }

	if (context->failed || context->in_call) { context->failed = true; return C1H_OWNER_UNAVAILABLE; }

	CallScope scope(*context);
	return context->console.attachFixtureOperator(operator_instance) ? C1H_OK : C1H_OWNER_UNAVAILABLE;
}

int c1h_operator_detach(c1h_context *context)
{
	if (context == nullptr) { return C1H_INVALID_ARGUMENT; }

	if (context->in_call) { context->failed = true; return C1H_OWNER_UNAVAILABLE; }

	CallScope scope(*context);
	return context->console.detachFixtureOperator() ? C1H_OK : C1H_OWNER_UNAVAILABLE;
}

int c1h_operator_prepare(c1h_context *context, uint32_t intent, uint64_t source,
			 uint64_t session, c1h_result *output)
{
	const int availability = available(context, output);

	if (availability != C1H_OK) { return availability; }

	CallScope scope(*context);

	if (intent > static_cast<uint32_t>(Intent::ClearLatch)) {
		output->result_code = C1H_INVALID_ARGUMENT;
		return finish(*context, *output);
	}

	if (sampleAndTick(*context, *output)) {
		captureConsole(context->console.prepare(static_cast<Intent>(intent), {source, session}), *output);
	}

	return finish(*context, *output);
}

int c1h_operator_confirm(c1h_context *context, uint64_t ticket, c1h_result *output)
{
	const int availability = available(context, output);

	if (availability != C1H_OK) { return availability; }

	CallScope scope(*context);

	if (sampleAndTick(*context, *output)) {
		const uint64_t prior_report = context->snapshot.last_report_sequence;
		captureConsole(context->console.confirm(ticket), *output);
		AdminResult audit{};

		// Failed prepare/confirm must not re-emit a prior administration result.
		if (context->broker.copyLastAdminResult(audit) && audit.snapshot.last_report_sequence > prior_report) {
			appendResult(*context, audit, 3, *output);
		}
	}

	return finish(*context, *output);
}
