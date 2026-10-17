"""Bounded host measurement window; not an operational real-time guarantee."""

from contextlib import contextmanager
import gc
import time


@contextmanager
def campaign_window():
    """Move cyclic collection outside the short, trace-capped nominal campaign.

    CPython reference counting remains enabled. Preserve the process's prior GC
    setting on success or exception. The caller caps retained trace rows and all
    I/O waits; a two-second host-clock overrun is a failed experiment, not an
    extension of any request deadline. This policy is local to this test process.
    """
    enabled = gc.isenabled()
    if enabled:
        gc.collect()
    try:
        if enabled:
            gc.disable()
        start = time.monotonic_ns()
        yield
        elapsed = time.monotonic_ns() - start
        if elapsed < 0 or elapsed > 2_000_000_000:
            raise RuntimeError("bounded nominal campaign exceeded two seconds")
    finally:
        if enabled:
            gc.enable()
