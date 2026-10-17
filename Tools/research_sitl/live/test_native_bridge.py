"""Native-boundary unit tests with explicit fake clocks and fake libraries.

No shared library, socket, key, or live clock qualification is created here.
True qualification flags on ClockStub are inputs to the boundary under test,
not evidence that the stub provides an approved clock.
"""

import ctypes as C
import unittest
from unittest.mock import Mock

from host_clock import ClockQualificationError, HostClock
from native_bridge import Config, NativeOwner, OperatorDriver, Result


class ClockStub:
    """Explicit unit-test boundary input, never used by a live runner."""

    def __init__(self, *, qualified=True, live_provider=True):
        self.qualified = qualified
        self.live_provider = live_provider
        self.check_environment = Mock()
        self.now_ns = Mock(side_effect=[1_000_000, 2_000_000])


class LibraryStub:
    """Record lookups as well as calls; never resolve a native symbol."""

    def __init__(self):
        self.lookups = []
        self.calls = []
        self.create_code = 0
        self.create_handle = 0x1234
        self.operation_code = 0
        self.operation_error = None
        self.result_edits = {}

    def __getattr__(self, name):
        if not name.startswith("c1h_"):
            raise AttributeError(name)
        self.lookups.append(name)

        def invoke(*arguments):
            self.calls.append((name, arguments))
            if name == "c1h_create":
                C.cast(arguments[1], C.POINTER(C.c_void_p))[0] = C.c_void_p(self.create_handle)
                return self.create_code
            if name == "c1h_destroy":
                return None
            if name in ("c1h_operator_attach", "c1h_operator_detach"):
                return 0
            if name not in ("c1h_tick", "c1h_receive", "c1h_access",
                            "c1h_operator_prepare", "c1h_operator_confirm"):
                raise AssertionError("unexpected native dispatch: " + name)
            if self.operation_error is not None:
                raise self.operation_error
            result = C.cast(arguments[-1], C.POINTER(Result)).contents
            result.now_us = 1001
            result.completion_us = 1002
            for field, value in self.result_edits.items():
                setattr(result, field, value)
            return self.operation_code

        return invoke


class NativeBridgeTests(unittest.TestCase):
    def make_owner(self):
        library, clock = LibraryStub(), ClockStub()
        config = Config(abi_version=1, struct_size=C.sizeof(Config))
        owner = NativeOwner(library, config, clock)
        self.addCleanup(owner.close)
        return owner, library, clock

    def assert_permanently_closed(self, owner, library):
        self.assertTrue(owner.closed)
        self.assertFalse(owner.context)
        self.assertEqual(sum(name == "c1h_destroy" for name, _ in library.calls), 1)
        calls, lookups = list(library.calls), list(library.lookups)
        with self.assertRaisesRegex(RuntimeError, "closed"):
            owner.call("access")
        owner.close()
        self.assertEqual(library.calls, calls)
        self.assertEqual(library.lookups, lookups)

    def test_unqualified_or_injected_clock_rejected_before_library_lookup(self):
        for qualified, live in ((False, False), (False, True), (True, False)):
            with self.subTest(qualified=qualified, live=live):
                library = LibraryStub()
                clock = ClockStub(qualified=qualified, live_provider=live)
                with self.assertRaisesRegex(RuntimeError, "qualified live clock"):
                    NativeOwner(library, Config(), clock)
                self.assertEqual(library.lookups, [])
                clock.check_environment.assert_not_called()

    def test_actual_hostclock_test_reader_rejected_before_create(self):
        library = LibraryStub()
        with self.assertRaisesRegex(RuntimeError, "qualified live clock"):
            NativeOwner(library, Config(), HostClock(lambda: 1234))
        self.assertEqual(library.lookups, [])

    def test_constructor_environment_fault_prevents_create(self):
        library, clock = LibraryStub(), ClockStub()
        clock.check_environment.side_effect = ClockQualificationError("boot identity changed")
        with self.assertRaisesRegex(ClockQualificationError, "boot identity"):
            NativeOwner(library, Config(), clock)
        self.assertEqual(library.lookups, [])

    def test_create_failure_or_null_handle_rejected(self):
        for code, handle in ((1, 0), (0, 0)):
            with self.subTest(code=code, handle=handle):
                library, clock = LibraryStub(), ClockStub()
                library.create_code, library.create_handle = code, handle
                with self.assertRaisesRegex(RuntimeError, "rejected configuration"):
                    NativeOwner(library, Config(), clock)
                self.assertEqual(library.lookups, ["c1h_create"])

    def test_valid_operation_rechecks_environment_and_preserves_bracket(self):
        owner, library, clock = self.make_owner()
        result, pre, post = owner.call("access")
        self.assertEqual((pre, post), (1_000_000, 2_000_000))
        self.assertEqual((result.now_us, result.completion_us), (1001, 1002))
        self.assertEqual(clock.check_environment.call_count, 2)
        self.assertEqual(library.lookups, ["c1h_create", "c1h_access"])
        self.assertFalse(owner.closed)

    def test_changed_metadata_closes_without_operation_lookup(self):
        owner, library, clock = self.make_owner()
        clock.check_environment.side_effect = ClockQualificationError("namespace changed")
        with self.assertRaisesRegex(ClockQualificationError, "namespace changed"):
            owner.call("access")
        self.assertEqual(library.lookups, ["c1h_create", "c1h_destroy"])
        clock.now_ns.assert_not_called()
        self.assert_permanently_closed(owner, library)

    def test_clock_read_fault_closes_before_native_operation(self):
        owner, library, clock = self.make_owner()
        clock.now_ns.side_effect = ClockQualificationError("clock read failed")
        with self.assertRaisesRegex(ClockQualificationError, "clock read failed"):
            owner.call("access")
        self.assertNotIn("c1h_access", library.lookups)
        self.assert_permanently_closed(owner, library)

    def test_post_call_clock_regression_closes_after_native_operation(self):
        owner, library, clock = self.make_owner()
        clock.now_ns.side_effect = [1_000_000, ClockQualificationError("monotonic clock regressed")]
        with self.assertRaisesRegex(ClockQualificationError, "regressed"):
            owner.call("access")
        self.assertIn("c1h_access", library.lookups)
        self.assert_permanently_closed(owner, library)

    def test_abi_faults_close_permanently(self):
        for field, value in (("abi_version", 2), ("struct_size", 1),
                             ("report_count", 9), ("reserved", 1)):
            with self.subTest(field=field):
                owner, library, _ = self.make_owner()
                library.result_edits[field] = value
                with self.assertRaisesRegex(RuntimeError, "invalid result ABI"):
                    owner.call("access")
                self.assert_permanently_closed(owner, library)

    def test_native_errors_close_permanently(self):
        for failure in ("return", "result", "exception"):
            with self.subTest(failure=failure):
                owner, library, _ = self.make_owner()
                if failure == "return":
                    library.operation_code = 1
                elif failure == "result":
                    library.result_edits["result_code"] = 1
                else:
                    library.operation_error = RuntimeError("FFI failure")
                with self.assertRaises(RuntimeError):
                    owner.call("access")
                self.assert_permanently_closed(owner, library)

    def test_bracket_faults_close_permanently(self):
        for edits in ({"now_us": 999}, {"completion_us": 2001},
                      {"now_us": 1002, "completion_us": 1001}):
            with self.subTest(edits=edits):
                owner, library, _ = self.make_owner()
                library.result_edits = edits
                with self.assertRaisesRegex(RuntimeError, "shared-clock bracket"):
                    owner.call("access")
                self.assert_permanently_closed(owner, library)

    def test_reversed_python_bracket_cannot_pass(self):
        owner, library, clock = self.make_owner()
        clock.now_ns.side_effect = [2_000_000, 1_000_000]
        with self.assertRaisesRegex(RuntimeError, "shared-clock bracket"):
            owner.call("access")
        self.assert_permanently_closed(owner, library)

    def test_unsupported_operations_never_lookup_or_dispatch(self):
        owner, library, clock = self.make_owner()
        for name in ("destroy", "create", "now_ns", "operator_attach", "operator_detach",
                     "config_size", "result_size", "__dict__", "", None):
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, "unsupported native operation"):
                    owner.call(name)
        self.assertEqual(library.lookups, ["c1h_create"])
        self.assertEqual(clock.check_environment.call_count, 1)
        clock.now_ns.assert_not_called()
        self.assertFalse(owner.closed)

    def test_receive_copies_bytes_and_converts_receipt_units(self):
        owner, library, _ = self.make_owner()
        data = b"unit-test-datagram"
        owner.receive(data, 1_000_999, 7)
        name, arguments = library.calls[-1]
        self.assertEqual(name, "c1h_receive")
        self.assertEqual(bytes(arguments[1]), data)
        self.assertEqual(arguments[2:5], (len(data), 1000, 7))

    def test_operator_attach_refuses_closed_handle_without_library_lookup(self):
        owner, library, _ = self.make_owner()
        owner.close()
        lookups = list(library.lookups)
        with self.assertRaisesRegex(RuntimeError, "closed"):
            OperatorDriver(owner, 55)
        self.assertEqual(library.lookups, lookups)

    def test_operator_detach_refuses_closed_handle_without_library_lookup(self):
        owner, library, _ = self.make_owner()
        operator = OperatorDriver(owner, 55)
        owner.close()
        lookups = list(library.lookups)
        with self.assertRaisesRegex(RuntimeError, "closed"):
            operator.detach()
        self.assertEqual(library.lookups, lookups)


if __name__ == "__main__":
    unittest.main()
