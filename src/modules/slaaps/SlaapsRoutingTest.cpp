// Synthetic uORB reports only. Never compiled into the flight/board target.
#include <px4_platform_common/px4_config.h>

#if !defined(CONFIG_SLAAPS_ROUTING_TEST) || !defined(__PX4_POSIX) || defined(ENABLE_LOCKSTEP_SCHEDULER)
#error SLAAPS routing fixture requires the explicit native wall-clock test target
#endif

#include <px4_platform_common/log.h>
#include <drivers/drv_hrt.h>
#include <uORB/PublicationMulti.hpp>
#include <uORB/topics/slaaps_output_bank.h>
#include <cstring>

extern "C" __EXPORT int slaaps_routing_test_main(int argc, char *argv[])
{
	if (argc != 2 || (strcmp(argv[1], "valid") && strcmp(argv[1], "duplicate") && strcmp(argv[1], "unavailable")
			  && strcmp(argv[1], "calibration") && strcmp(argv[1], "stale"))) { return PX4_ERROR; }

	static uORB::PublicationMulti<slaaps_output_bank_s> publishers[2] {
		uORB::PublicationMulti<slaaps_output_bank_s>{ORB_ID(slaaps_output_bank)},
		uORB::PublicationMulti<slaaps_output_bank_s>{ORB_ID(slaaps_output_bank)}
	};

	for (unsigned bank = 0; bank < 2; ++bank) {
		slaaps_output_bank_s report{};
		report.timestamp = strcmp(argv[1], "stale") ? hrt_absolute_time() : 1;
		report.driver_instance = bank + 1;
		report.hardware_mask = 255;
		report.bank = bank;
		report.channel_count = 8;
		report.ready = true;

		for (unsigned channel = 0; channel < 8; ++channel) {
			report.rate_hz[channel] = 50;
			report.minimum_us[channel] = 1100;
			report.center_us[channel] = 1500;
			report.maximum_us[channel] = 1900;
		}

		if (bank == 0) {
			report.function[7] = 10100;
			report.function[4] = 10102;

		} else {
			report.function[2] = 10101;
			report.function[0] = 10103;
			report.function[6] = 10104;

			if (!strcmp(argv[1], "duplicate")) { report.function[7] = 10100; }

			if (!strcmp(argv[1], "unavailable")) { report.hardware_mask &= ~(1U << 2); }

			if (!strcmp(argv[1], "calibration")) { report.center_us[2] = 1501; }
		}

		publishers[bank].publish(report);
	}

	return PX4_OK;
}
