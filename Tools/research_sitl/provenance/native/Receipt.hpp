#pragma once

#if !defined(RESEARCH_PROVENANCE_CAPTURE)
#error "Private observation profile only"
#endif

#include <cstdint>

// Value observation at the completed EKF output boundary, not admission state.
// Caller is the single-instance EKF work queue in the private source view.
extern "C" void research_capture_ekf(const void *instance, uint64_t sample_us,
				     bool replay, bool multi, bool attitude_valid,
				     bool horizontal_valid, bool vertical_position_valid,
				     bool vertical_velocity_valid, uint64_t control_flags,
				     uint32_t fault_flags) noexcept;
