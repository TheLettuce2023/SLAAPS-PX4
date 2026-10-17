"""Clock-contract failures and conservative host-observation scoring."""

import unittest
import time
from unittest.mock import patch

from host_clock import (
    ClockMapInterval, ClockQualificationError, HostClock, ObservationWindow,
    PYTIME_NS_MAX, UINT64_MAX, UNCERTAINTY_US,
)


def environment():
    return {
        "python_implementation": "cpython", "python_version": [3, 12, 3],
        "platform": "linux", "kernel_release": "6.6.114.1-microsoft-standard-WSL2",
        "distribution": "ubuntu", "distribution_version": "24.04",
        "clock_implementation": "clock_gettime(CLOCK_MONOTONIC)",
        "monotonic": True, "adjustable": False,
        "resolution_seconds": 1e-9, "clock_getres_seconds": 1e-9,
        "boot_id": "test-boot", "time_namespace": "time:[4026531834]",
        "time_namespace_offsets": "monotonic 0 0\nboottime 0 0",
        "clocksource": "tsc", "process_id": 123,
    }


class ClockTests(unittest.TestCase):
    def qualify(self, values=(1000, 2000), native=1500):
        clock = HostClock(iter(values).__next__)
        with patch("host_clock._environment", return_value=environment()):
            manifest = clock.qualify(lambda: native)
        return clock, manifest

    def qualify_live(self):
        # Real default reader and real Linux clock read, with real metadata.
        # This tests this WSL environment, not the native admission library.
        clock = HostClock()
        evidence = clock.qualify(lambda: time.clock_gettime_ns(time.CLOCK_MONOTONIC))
        return clock, evidence

    def test_native_shared_domain_bracket_is_diagnostic_for_injected_reader(self):
        clock, evidence = self.qualify()
        self.assertFalse(clock.qualified)
        self.assertFalse(clock.live_qualified)
        self.assertFalse(clock.live_provider)
        self.assertFalse(evidence["qualified"])
        self.assertFalse(evidence["live_qualified"])
        self.assertTrue(evidence["native_bracket_verified"])
        self.assertEqual(evidence["provider_kind"], "injected_test")
        self.assertEqual(evidence["native_sample_ns"], 1500)
        with self.assertRaises(ClockQualificationError):
            clock.map_interval(1, 100)

    def test_real_default_provider_and_manifest_isolation(self):
        clock, evidence = self.qualify_live()
        self.assertTrue(clock.qualified)
        self.assertTrue(clock.live_qualified)
        self.assertTrue(clock.live_provider)
        self.assertTrue(evidence["live_qualified"])
        self.assertEqual(evidence["provider_kind"], "live_default")
        self.assertEqual(evidence["uncertainty_us"], 1)
        self.assertEqual(evidence["offset_us"], 0)
        self.assertLessEqual(evidence["python_before_ns"], evidence["native_sample_ns"])
        self.assertLessEqual(evidence["native_sample_ns"], evidence["python_after_ns"])
        evidence["qualified"] = False
        evidence["live_qualified"] = False
        evidence["environment"]["clocksource"] = "changed"
        evidence["environment"]["python_version"][0] = 99
        self.assertTrue(clock.qualified)
        clock.check_environment()
        start = clock.now_us()
        self.assertTrue(clock.map_interval(start, 100).covers(start, start + 80_000))

    def test_injected_real_function_cannot_become_live(self):
        clock = HostClock(time.monotonic_ns)
        self.assertFalse(clock.live_provider)
        with self.assertRaises(AttributeError):
            clock.live_provider = True
        for _ in range(2):
            evidence = clock.qualify(lambda: time.clock_gettime_ns(time.CLOCK_MONOTONIC))
            self.assertTrue(evidence["native_bracket_verified"])
            self.assertFalse(evidence["live_qualified"])
            self.assertFalse(clock.live_qualified)
        with self.assertRaises(ClockQualificationError):
            clock.map_interval(clock.now_us(), 100)

    def test_diagnostic_manifest_cannot_authorize_live_operations(self):
        for operation in ("map", "environment"):
            with self.subTest(operation=operation):
                clock, evidence = self.qualify()
                evidence.update(qualified=True, live_qualified=True, provider_kind="live_default")
                self.assertFalse(clock.qualified)
                self.assertFalse(clock.live_qualified)
                with self.assertRaises(ClockQualificationError):
                    if operation == "map":
                        clock.map_interval(1, 100)
                    else:
                        clock.check_environment()

    def test_missing_native_stays_unqualified(self):
        clock = HostClock()
        evidence = clock.qualify()
        self.assertFalse(evidence["qualified"])
        self.assertFalse(evidence["native_bracket_verified"])
        self.assertFalse(clock.live_qualified)
        with self.assertRaises(ClockQualificationError):
            clock.map_interval(1, 100)

    def test_wrong_provider_and_nonzero_namespace_refused(self):
        for field, value in (
            ("clock_implementation", "clock_gettime(CLOCK_BOOTTIME)"),
            ("monotonic", False), ("adjustable", True),
            ("time_namespace_offsets", "monotonic 1 0\nboottime 0 0"),
            ("boot_id", ""), ("platform", "win32"),
        ):
            with self.subTest(field=field):
                data = environment()
                data[field] = value
                clock = HostClock(lambda: 1000)
                with patch("host_clock._environment", return_value=data):
                    with self.assertRaises(ClockQualificationError):
                        clock.qualify(lambda: 1000)
                self.assertFalse(clock.qualified)

    def test_native_outside_bracket_refused(self):
        for native in (999, 2001, -1, PYTIME_NS_MAX + 1, True):
            with self.subTest(native=native), self.assertRaises(ClockQualificationError):
                self.qualify(native=native)

    def test_regression_latches_even_after_recovery(self):
        clock = HostClock(iter((2001, 2000, 3000)).__next__)
        self.assertEqual(clock.now_us(), 2)
        with self.assertRaisesRegex(ClockQualificationError, "regressed"):
            clock.now_ns()
        with self.assertRaisesRegex(ClockQualificationError, "regressed"):
            clock.now_ns()

    def test_equal_clock_readings_valid(self):
        clock = HostClock(lambda: 1999)
        self.assertEqual(clock.now_us(), 1)
        self.assertEqual(clock.now_us(), 1)

    def test_read_overflow_and_wrong_types_refused(self):
        for value in (-1, PYTIME_NS_MAX + 1, 1000.0, True):
            with self.subTest(value=value), self.assertRaises(ClockQualificationError):
                HostClock(lambda: value).now_ns()

    def test_identity_change_invalidates_qualification(self):
        for field, replacement in (
            ("boot_id", "new-boot"), ("process_id", 124),
            ("time_namespace", "time:[4026531835]"), ("clocksource", "hpet"),
        ):
            with self.subTest(field=field):
                clock, evidence = self.qualify_live()
                data = evidence["environment"]
                data[field] = replacement
                with patch("host_clock._environment", return_value=data):
                    with self.assertRaises(ClockQualificationError):
                        clock.check_environment()
                self.assertFalse(clock.qualified)
                self.assertFalse(clock.live_qualified)

    def test_valid_provenance_recheck(self):
        clock, _ = self.qualify_live()
        clock.check_environment()
        self.assertTrue(clock.qualified)


class MapAndObservationTests(unittest.TestCase):
    def test_complete_expiry_endpoint_coverage(self):
        interval = ClockMapInterval.for_run(100, 1_000_000)
        self.assertEqual(interval.valid_until_us, 1_080_100)
        self.assertTrue(interval.covers(1_000_100, 1_080_100))
        self.assertFalse(interval.covers(1_000_101, 1_080_101))
        self.assertFalse(interval.covers(99, 80_099))
        self.assertFalse(interval.covers(100, 100))

    def test_map_arithmetic_refuses_wrap(self):
        with self.assertRaises(ClockQualificationError):
            ClockMapInterval.for_run(UINT64_MAX - 80_000, 1)
        with self.assertRaises(ClockQualificationError):
            ClockMapInterval(2, 1)
        with self.assertRaises(ClockQualificationError):
            ClockMapInterval(0, 100).covers(0, UINT64_MAX + 1)

    def test_quantization_extrema_over_whole_lifetime(self):
        # Samples exercise the algebra in the module docstring; observations
        # never establish a bound by themselves. Exact integer residuals avoid
        # float loss even near the maximum representable clock value.
        for base in (0, 1_000_000, (PYTIME_NS_MAX // 1000 - 80_001) * 1000):
            for duration in (0, 1, 79_999, 80_000):
                for first in (0, 1, 998, 999):
                    for last in (0, 1, 998, 999):
                        a, b = base + first, base + duration * 1000 + last
                        represented = (b // 1000 - a // 1000) * 1000
                        self.assertLess(abs((b - a) - represented), UNCERTAINTY_US * 1000)

    def test_post_call_delay_counts_against_target(self):
        receipt = ObservationWindow(1_000_000, 1_001_000)
        admitted = ObservationWindow(4_000_000, 5_001_000)
        self.assertEqual(admitted.elapsed_bounds_ns(receipt), (2_999_000, 4_001_000))
        self.assertFalse(admitted.within_us(receipt, 4000))
        self.assertTrue(admitted.within_us(receipt, 4001))

    def test_ambiguous_or_regressed_observations_cannot_pass(self):
        with self.assertRaises(ClockQualificationError):
            ObservationWindow(2, 1)
        with self.assertRaises(ClockQualificationError):
            ObservationWindow(0, 1).within_us(ObservationWindow(2, 3), 1)
        self.assertFalse(ObservationWindow(2, 4).within_us(ObservationWindow(1, 3), 1))


if __name__ == "__main__":
    unittest.main()
