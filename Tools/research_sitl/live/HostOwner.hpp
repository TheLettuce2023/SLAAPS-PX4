#pragma once

// Host-only C ABI. These functions never publish to PX4 or own a socket.
// The trusted runner serializes all calls and keeps this handle from guidance.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { C1H_ABI_VERSION = 1, C1H_MAX_REPORTS = 8, C1H_MAX_FRAME = 280, C1H_STATUS_PAYLOAD = 106 };
enum {
	C1H_OK = 0, C1H_INVALID_ARGUMENT = 1, C1H_INVALID_CONFIG = 2,
	C1H_CLOCK_FAULT = 3, C1H_OWNER_UNAVAILABLE = 4, C1H_STATUS_FAILURE = 5,
	C1H_RESOURCE_FAILURE = 6
};
enum {
	C1H_SIM_ARMED = 1, C1H_SIM_SELECTED = 2, C1H_SIM_CONNECTED = 4,
	C1H_SIM_REQUIRED_VALID = 8, C1H_SIM_MAP_VALID = 16
};
// Intent values match the redacted broker: establish=0, revoke=1, clear=2.
typedef struct c1h_context c1h_context;

typedef struct {
	uint32_t abi_version;
	uint32_t struct_size;
	uint8_t key[32];
	uint64_t run_id;
	uint64_t source_id;
	uint64_t session_id;
	uint64_t controller_id;
	uint64_t channel_id;
	uint64_t map_epoch;
	uint64_t signing_seed;
	uint64_t map_start_us;
	uint64_t map_end_us;
	uint64_t maximum_receipt_age_us;
	uint64_t maximum_confirmation_age_us;
} c1h_config;

typedef struct {
	uint64_t report_sequence;
	uint64_t transition_epoch;
	uint64_t validator_time_us;
	uint32_t frame_length;
	uint32_t status_class;
	uint32_t decision_code;
	uint32_t status_flags;
	uint8_t payload[C1H_STATUS_PAYLOAD];
	uint8_t frame[C1H_MAX_FRAME];
} c1h_status;

typedef struct {
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t result_code;
	uint32_t code_category; // 0=none, 1=adapter, 2=ingress, 3=administration.
	uint32_t native_code;
	uint32_t parser_reason;
	uint32_t packet_kind;
	uint32_t signature_verified;
	uint32_t receipt_offered;
	uint32_t report_count;
	uint32_t terminal;
	uint32_t admission_closed;
	uint32_t has_eligible_request;
	uint32_t broker_code;
	uint32_t console_disposition;
	uint32_t event_consumed;
	uint32_t has_adapter_code;
	uint32_t adapter_code;
	uint64_t now_us; // Actual native CLOCK_MONOTONIC sample used by last operation.
	uint64_t completion_us;
	uint64_t received_us; // Observation supplied by the trusted socket runner.
	uint64_t state_generation;
	uint64_t transition_epoch;
	uint64_t barrier_report_sequence;
	uint64_t barrier_time_us;
	uint64_t last_report_sequence;
	uint64_t ticket;
	uint64_t request_sequence;
	uint64_t request_created_us;
	uint64_t request_expiry_us;
	float acceleration_frd_m_s2[3];
	uint32_t reserved;
	c1h_status reports[C1H_MAX_REPORTS];
} c1h_result;

uint32_t c1h_config_size(void);
uint32_t c1h_result_size(void);
uint64_t c1h_now_ns(void); // Zero indicates a native clock-read/arithmetic failure.
// Every result pointer must reference at least the two uint32_t ABI header
// fields, initialized to C1H_ABI_VERSION and sizeof(c1h_result), respectively.
// A matching header promises writable storage for the complete result.
// Unknown versions/sizes return C1H_INVALID_ARGUMENT without modifying output.
int c1h_create(const c1h_config *config, c1h_context **output);
void c1h_destroy(c1h_context *context);
int c1h_tick(c1h_context *context, uint32_t simulated_state_flags, c1h_result *output);
int c1h_receive(c1h_context *context, const uint8_t *bytes, uint32_t length,
		uint64_t received_us, uint64_t channel_id, c1h_result *output);
int c1h_access(c1h_context *context, c1h_result *output);
int c1h_operator_attach(c1h_context *context, uint64_t operator_instance);
int c1h_operator_detach(c1h_context *context);
int c1h_operator_prepare(c1h_context *context, uint32_t intent, uint64_t source,
			 uint64_t session, c1h_result *output);
int c1h_operator_confirm(c1h_context *context, uint64_t ticket, c1h_result *output);

#ifdef __cplusplus
}
#endif
