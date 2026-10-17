// C1A passive diagnostic only. No admission owner, publisher, network or output.
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/time.h>
#include <drivers/drv_hrt.h>
#include <uORB/Subscription.hpp>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/vehicle_status.h>

#include <cinttypes>
#include <cstdint>
#include <time.h>

#if !defined(__PX4_POSIX) || !defined(__linux__) || !defined(CONFIG_ARCH_BOARD_PX4_SITL)
#error "C1A research_probe is Linux POSIX PX4 SITL only"
#endif

#if defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "C1A research_probe requires non-lockstep, non-proxy native clocks"
#endif

namespace
{
constexpr uint64_t NanosecondsPerSecond = 1000000000;
constexpr uint64_t NanosecondsPerMicrosecond = 1000;
constexpr uint64_t WindowNs = 2 * NanosecondsPerSecond;
constexpr useconds_t SampleIntervalUs = 100000; // At most 10 Hz diagnostic sampling; not an operational deadline.
constexpr unsigned MinimumFresh = 2;

const char *jsonBool(bool value)
{
	return value ? "true" : "false";
}

bool hostNow(uint64_t &value)
{
	timespec sample{};

	// system_clock_gettime is the repository's explicit real-libc alias.
	if (system_clock_gettime(CLOCK_MONOTONIC, &sample) != 0 || sample.tv_sec < 0
	    || sample.tv_nsec < 0 || sample.tv_nsec >= static_cast<long>(NanosecondsPerSecond)) {
		return false;
	}

	const uint64_t seconds = static_cast<uint64_t>(sample.tv_sec);
	const uint64_t nanoseconds = static_cast<uint64_t>(sample.tv_nsec);

	if (seconds > (UINT64_MAX - nanoseconds) / NanosecondsPerSecond) {
		return false;
	}

	value = seconds * NanosecondsPerSecond + nanoseconds;
	return value != 0;
}

struct ClockSample {
	uint64_t before{};
	uint64_t hrt{};
	uint64_t after{};
};

bool sampleClock(ClockSample &sample, const ClockSample &previous)
{
	if (!hostNow(sample.before)) {
		return false;
	}

	sample.hrt = hrt_absolute_time();

	if (!hostNow(sample.after)) {
		return false;
	}

	// Compact keys keep redirected, non-color rows below PX4's 127-byte log
	// limit: b/a = host CLOCK_MONOTONIC ns, h = PX4 hrt us.
	PX4_INFO("C1A {\"t\":\"clock\",\"b\":%" PRIu64 ",\"h\":%" PRIu64
		 ",\"a\":%" PRIu64 "}", sample.before, sample.hrt, sample.after);

	if (sample.hrt == 0 || sample.hrt > (UINT64_MAX - (NanosecondsPerMicrosecond - 1)) / NanosecondsPerMicrosecond
	    || sample.after < sample.before || sample.before < previous.after || sample.hrt < previous.hrt) {
		return false;
	}

	const uint64_t hrt_ns = sample.hrt * NanosecondsPerMicrosecond;
	// Consistency under the explicit native-clock build guards, not a qualified map.
	return hrt_ns <= sample.after && sample.before <= hrt_ns + NanosecondsPerMicrosecond - 1;
}

struct Observations {
	unsigned fresh{};
	unsigned generation{};
	bool seen{};
	uint64_t timestamp{};

	bool record(uint64_t stamp, unsigned current_generation, uint64_t start, uint64_t now,
		    bool &is_fresh, unsigned &generation_delta)
	{
		generation_delta = seen ? current_generation - generation : 0;
		is_fresh = stamp >= start && stamp > timestamp;
		const bool valid = stamp != 0 && stamp <= now && (!seen || (stamp > timestamp && generation_delta != 0));

		if (valid && is_fresh) {
			++fresh;
		}

		seen = true;
		timestamp = stamp;
		generation = current_generation;
		return valid;
	}
};
} // namespace

extern "C" __EXPORT int research_probe_main(int argc, char *argv[])
{
	(void)argv;

	if (argc != 1) {
		PX4_ERR("research_probe takes no arguments");
		return 1;
	}

	uORB::Subscription status_sub{ORB_ID(vehicle_status)};
	uORB::Subscription armed_sub{ORB_ID(actuator_armed)};
	Observations status_observations{};
	Observations armed_observations{};
	ClockSample previous{};
	ClockSample start{};
	bool clocks_valid = sampleClock(start, previous);
	bool observations_valid = true;
	bool all_disarmed = true;
	bool complete = false;
	unsigned clock_samples = clocks_valid ? 1 : 0;
	uint64_t elapsed = 0;
	previous = start;

	while (clocks_valid && observations_valid && all_disarmed && !complete) {
		if (system_usleep(SampleIntervalUs) != 0) {
			break;
		}

		vehicle_status_s status{};
		actuator_armed_s armed{};
		const bool status_updated = status_sub.update(&status);
		const bool armed_updated = armed_sub.update(&armed);
		ClockSample current{};
		clocks_valid = sampleClock(current, previous);

		if (!clocks_valid) {
			break;
		}

		++clock_samples;
		elapsed = current.after - start.before;
		previous = current;

		if (status_updated) {
			bool fresh = false;
			unsigned delta = 0;
			const unsigned generation = status_sub.get_last_generation();
			const bool valid = status_observations.record(status.timestamp, generation, start.hrt, current.hrt, fresh, delta);
			observations_valid = observations_valid && valid;
			all_disarmed = all_disarmed && status.arming_state == vehicle_status_s::ARMING_STATE_DISARMED;
			PX4_INFO("C1A {\"t\":\"status\",\"us\":%" PRIu64
				 ",\"g\":%u,\"d\":%u,\"f\":%s,\"s\":%u}",
				 status.timestamp, generation, delta, jsonBool(fresh), static_cast<unsigned>(status.arming_state));
		}

		if (armed_updated) {
			bool fresh = false;
			unsigned delta = 0;
			const unsigned generation = armed_sub.get_last_generation();
			const bool valid = armed_observations.record(armed.timestamp, generation, start.hrt, current.hrt, fresh, delta);
			observations_valid = observations_valid && valid;
			all_disarmed = all_disarmed && !armed.armed;
			PX4_INFO("C1A {\"t\":\"armed\",\"us\":%" PRIu64
				 ",\"g\":%u,\"d\":%u,\"f\":%s,\"a\":%s}",
				 armed.timestamp, generation, delta, jsonBool(fresh), jsonBool(armed.armed));
		}

		complete = elapsed >= WindowNs;
	}

	// Generation deltas expose missed publications; they do not describe every
	// internal Commander transition. No lossless-transition or admission claim.
	const bool pass = complete && clocks_valid && observations_valid && all_disarmed && clock_samples >= MinimumFresh
			  && status_observations.fresh >= MinimumFresh && armed_observations.fresh >= MinimumFresh;
	// c = valid clocks, v/a = fresh status/armed counts, ns = elapsed host ns.
	PX4_INFO("C1A {\"t\":\"counts\",\"c\":%u,\"v\":%u,\"a\":%u,\"ns\":%" PRIu64 "}",
		 clock_samples, status_observations.fresh, armed_observations.fresh, elapsed);
	PX4_INFO("C1A {\"t\":\"summary\",\"complete\":%s,\"pass\":%s,\"disarmed\":%s,\"clocks\":%s,\"obs\":%s}",
		 jsonBool(complete), jsonBool(pass), jsonBool(all_disarmed), jsonBool(clocks_valid), jsonBool(observations_valid));
	return pass ? 0 : 1;
}
