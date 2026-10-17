"""C1H clock evidence for one CPython/native process in one WSL namespace.

The reference timeline is the integer returned by CLOCK_MONOTONIC, not UTC
or an independently calibrated physical clock. For any nonnegative integer
reading C, C = 1000 * (C // 1000) + r with 0 <= r <= 999. Thus the difference
between a pair of ns readings and its pair of us representations has error
strictly below 1 us. Identity offset 0 and symmetric uncertainty 1 us cover
this representation error throughout every 80 ms lifetime, without drift
extrapolation. Reported clock resolution is metadata, not an accuracy bound.

CLOCK_MONOTONIC excludes Linux suspend. These helpers qualify host monotonic
observations only; they do not qualify wall time, VM pause behavior, physical
deadlines, Pi/PX4 synchronization, or scheduling. Call bracketing measures
scheduling/FFI delay separately and never folds it into map uncertainty.
"""

from dataclasses import asdict, dataclass
from copy import deepcopy
from pathlib import Path
import os
import platform
import sys
import time
from typing import Callable


NS_PER_US = 1000
UINT64_MAX = (1 << 64) - 1
PYTIME_NS_MAX = (1 << 63) - 1
UNCERTAINTY_US = 1
VALIDITY_US = 80_000


class ClockQualificationError(ValueError):
    """Unsupported or discontinuous clock evidence; never silently recover."""


def _integer(value: int, maximum: int, label: str) -> int:
    if type(value) is not int or not 0 <= value <= maximum:
        raise ClockQualificationError(f"{label} must be an integer in [0, {maximum}]")
    return value


def _environment() -> dict:
    """Read nonsecret provenance only; no namespace or clock configuration."""
    info = time.get_clock_info("monotonic")
    distro = platform.freedesktop_os_release()
    return {
        "python_implementation": sys.implementation.name,
        "python_version": list(sys.version_info[:3]),
        "platform": sys.platform,
        "kernel_release": platform.release(),
        "distribution": distro.get("ID"),
        "distribution_version": distro.get("VERSION_ID"),
        "clock_implementation": info.implementation,
        "monotonic": info.monotonic,
        "adjustable": info.adjustable,
        "resolution_seconds": info.resolution,
        "clock_getres_seconds": time.clock_getres(time.CLOCK_MONOTONIC),
        "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
        "time_namespace": os.readlink("/proc/self/ns/time"),
        "time_namespace_offsets": Path("/proc/self/timens_offsets").read_text().strip(),
        "clocksource": Path(
            "/sys/devices/system/clocksource/clocksource0/current_clocksource"
        ).read_text().strip(),
        "process_id": os.getpid(),
    }


def _validate_environment(evidence: dict) -> None:
    supported = (
        evidence.get("python_implementation") == "cpython"
        and evidence.get("python_version", [])[:2] == [3, 12]
        and evidence.get("platform") == "linux"
        and "microsoft" in evidence.get("kernel_release", "").lower()
        and evidence.get("distribution") == "ubuntu"
        and evidence.get("distribution_version") == "24.04"
        and evidence.get("clock_implementation") == "clock_gettime(CLOCK_MONOTONIC)"
        and evidence.get("monotonic") is True
        and evidence.get("adjustable") is False
        and evidence.get("resolution_seconds") == 1e-9
        and evidence.get("clock_getres_seconds") == 1e-9
    )
    if not supported:
        raise ClockQualificationError("unsupported C1H clock provider/environment")
    for field in ("boot_id", "time_namespace", "time_namespace_offsets", "clocksource"):
        if not isinstance(evidence.get(field), str) or not evidence[field]:
            raise ClockQualificationError(f"missing clock provenance: {field}")
    # C1H deliberately supports the inspected initial namespace offsets only.
    offsets = [line.split() for line in evidence["time_namespace_offsets"].splitlines()]
    if sorted(offsets) != [["boottime", "0", "0"], ["monotonic", "0", "0"]]:
        raise ClockQualificationError("unsupported time namespace offsets")
    if not evidence["time_namespace"].startswith("time:["):
        raise ClockQualificationError("invalid time namespace identity")
    if type(evidence.get("process_id")) is not int or evidence["process_id"] <= 0:
        raise ClockQualificationError("invalid clock process identity")


class HostClock:
    """Owner-local, serialized clock reader with a permanent failure latch.

    Native qualification requires a callback returning CLOCK_MONOTONIC ns
    from the same process. A successful bracket checks integration, not
    statistical offset or physical accuracy. Source review establishes that
    both readers invoke the same clock. Passing no callback returns an
    explicitly unqualified manifest, useful before the bridge exists.
    Injected readers are diagnostic test fixtures: even a passing native
    bracket can never qualify them for a live map.
    """

    def __init__(self, read_ns: Callable[[], int] | None = None):
        # Classification is fixed at construction. Even explicitly injecting
        # time.monotonic_ns remains a test reader, never a live provider.
        self._default_provider = read_ns is None
        self._read_ns = time.monotonic_ns if read_ns is None else read_ns
        self._last_ns = None
        self._fault = None
        self._environment = None
        self._manifest = None

    @property
    def live_provider(self) -> bool:
        """Construction-time provenance; no setter or diagnostic promotion."""
        return self._default_provider

    @property
    def qualified(self) -> bool:
        """Compatibility alias: qualification always means live qualification."""
        return self.live_qualified

    @property
    def live_qualified(self) -> bool:
        """True only for the default real reader with verified native evidence."""
        return (self._default_provider and self._fault is None
                and self._manifest is not None and self._manifest["live_qualified"])

    def _fail(self, reason: str):
        if self._fault is None:
            self._fault = reason
        raise ClockQualificationError(self._fault)

    def now_ns(self) -> int:
        if self._fault is not None:
            self._fail(self._fault)
        try:
            value = _integer(self._read_ns(), PYTIME_NS_MAX, "clock ns")
        except Exception as exc:
            self._fail(f"clock read failed: {exc}")
        if self._last_ns is not None and value < self._last_ns:
            self._fail("monotonic clock regressed")
        self._last_ns = value
        return value

    def now_us(self) -> int:
        return self.now_ns() // NS_PER_US

    def qualify(self, native_now_ns: Callable[[], int] | None = None) -> dict:
        if self._fault is not None:
            self._fail(self._fault)
        try:
            evidence = _environment()
            _validate_environment(evidence)
            if self._environment is not None and evidence != self._environment:
                raise ClockQualificationError("clock provenance changed")
            self._environment = evidence
            before = self.now_ns()
            native = None if native_now_ns is None else _integer(
                native_now_ns(), PYTIME_NS_MAX, "native clock ns"
            )
            after = self.now_ns()
            if native is not None and not before <= native <= after:
                raise ClockQualificationError("native clock sample outside Python bracket")
            self._manifest = {
                "qualified": self._default_provider and native is not None,
                "live_qualified": self._default_provider and native is not None,
                "provider_kind": "live_default" if self._default_provider else "injected_test",
                "native_bracket_verified": native is not None,
                "environment": dict(evidence),
                "python_before_ns": before,
                "native_sample_ns": native,
                "python_after_ns": after,
                "offset_us": 0,
                "uncertainty_us": UNCERTAINTY_US,
                "uncertainty_basis": "shared CLOCK_MONOTONIC integer floor representation; <1 us interval error",
                "scope": "host CLOCK_MONOTONIC timeline; excludes physical accuracy and scheduling",
                "suspend_semantics": "Linux suspend excluded; VM pause behavior not qualified",
            }
            # Do not allow callers to mutate the internal qualification record.
            return deepcopy(self._manifest)
        except Exception as exc:
            self._fail(f"clock qualification failed: {exc}")

    def check_environment(self) -> None:
        """Fail closed on changed identity/provider; this is not clock calibration."""
        if not self.live_qualified:
            self._fail("clock is not qualified")
        try:
            current = _environment()
            _validate_environment(current)
            if current != self._environment:
                raise ClockQualificationError("clock provenance changed")
            self.now_ns()
        except Exception as exc:
            self._fail(f"clock environment check failed: {exc}")

    def map_interval(self, start_us: int, production_duration_us: int):
        if not self.live_qualified:
            self._fail("clock is not qualified")
        return ClockMapInterval.for_run(start_us, production_duration_us)


@dataclass(frozen=True)
class ClockMapInterval:
    """Inclusive producer coverage, including the exclusive expiry endpoint.

    The caller separately binds source/session/controller/map epoch. This
    interval is not itself trust or permission to install a native map.
    """

    valid_from_us: int
    valid_until_us: int

    def __post_init__(self):
        _integer(self.valid_from_us, UINT64_MAX, "map start")
        _integer(self.valid_until_us, UINT64_MAX, "map end")
        if self.valid_until_us < self.valid_from_us:
            raise ClockQualificationError("map interval reversed")

    @classmethod
    def for_run(cls, start_us: int, production_duration_us: int):
        _integer(start_us, UINT64_MAX, "run start")
        _integer(production_duration_us, UINT64_MAX, "production duration")
        end = start_us + production_duration_us + VALIDITY_US
        return cls(start_us, _integer(end, UINT64_MAX, "map end"))

    def covers(self, creation_us: int, expiry_us: int) -> bool:
        _integer(creation_us, UINT64_MAX, "creation")
        _integer(expiry_us, UINT64_MAX, "expiry")
        return self.valid_from_us <= creation_us < expiry_us <= self.valid_until_us

    def evidence(self) -> dict:
        return {**asdict(self), "offset_us": 0, "uncertainty_us": UNCERTAINTY_US}


@dataclass(frozen=True)
class ObservationWindow:
    """Actual pre/post clock observations enclosing one host operation.

    The operation's internal instant is unknown within this interval.
    Timing success uses its conservative upper bound; a wide bracket
    cannot become a pass by substituting its pre-call observation.
    """

    pre_ns: int
    post_ns: int

    def __post_init__(self):
        _integer(self.pre_ns, PYTIME_NS_MAX, "observation start")
        _integer(self.post_ns, PYTIME_NS_MAX, "observation end")
        if self.post_ns < self.pre_ns:
            raise ClockQualificationError("observation interval reversed")

    def elapsed_bounds_ns(self, earlier: "ObservationWindow") -> tuple[int, int]:
        if self.post_ns < earlier.pre_ns:
            raise ClockQualificationError("event order reversed")
        return self.pre_ns - earlier.post_ns, self.post_ns - earlier.pre_ns

    def within_us(self, earlier: "ObservationWindow", limit_us: int) -> bool:
        _integer(limit_us, UINT64_MAX, "timing limit")
        lower, upper = self.elapsed_bounds_ns(earlier)
        return lower >= 0 and upper <= limit_us * NS_PER_US
