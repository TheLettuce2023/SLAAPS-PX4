#pragma once
#if !defined(RESEARCH_P3C_NATIVE) || !defined(__PX4_POSIX) || !defined(__linux__) \
    || defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP) \
    || defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
#error "Private checked receipt clock observation profile only"
#endif
#include <cstdint>
enum class ClockPurpose : uint8_t { SourceStart, SourceEnd, SourceObservation, OwnerBefore, OwnerAfter, StopIntent, OwnerClose };
bool research_clock_begin(uint64_t run) noexcept;
uint64_t research_clock_read(ClockPurpose purpose) noexcept;
bool research_clock_faulted() noexcept;
bool research_clock_finish(const char *path) noexcept;
