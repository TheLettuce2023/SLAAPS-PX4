#pragma once

#if !defined(RESEARCH_P2G_SELECTOR_STATE) || !defined(RESEARCH_P2G_MULTI_SELECTOR) \
	|| !defined(RESEARCH_P2H_SELECTOR_DISPOSITION) || !defined(__PX4_POSIX) || !defined(__linux__) \
	|| defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2H selector disposition observations require the private P2G non-lockstep POSIX profile"
#endif

#include <cstdint>

struct DispositionEkfAttitudeObservation {
	uint64_t sample_us{};
	int32_t estimator_instance{};
	bool attitude_valid{};
	bool publication_attempted{};
	bool publication_succeeded{};
};

struct DispositionSelectorOutputObservation {
	uint64_t sample_us{};
	uint64_t source_publication_us{};
	uint64_t previous_sample_us{};
	uint64_t age_us{};
	float source_acceleration[3] {};
	uint8_t selected_instance{};
	uint8_t source_instance{};
	bool candidate_available{};
	bool monotonic{};
	bool fresh{};
	bool publish_requested{};
};

bool research_disposition_begin(uint64_t run_id) noexcept;
void research_disposition_selector_start(const void *selector) noexcept;
void research_disposition_estimator_start(const void *estimator, int32_t estimator_instance,
		bool replay, bool multi) noexcept;
void research_disposition_ekf_attitude(const void *estimator,
				       const DispositionEkfAttitudeObservation &observation) noexcept;
void research_disposition_attitude(const void *selector,
				   const DispositionSelectorOutputObservation &observation) noexcept;
void research_disposition_local_position(const void *selector,
		const DispositionSelectorOutputObservation &observation) noexcept;
bool research_disposition_prepare_stop() noexcept;
void research_disposition_selector_end(const void *selector) noexcept;
void research_disposition_estimator_end(const void *estimator) noexcept;
bool research_disposition_finish(const char *path) noexcept;
