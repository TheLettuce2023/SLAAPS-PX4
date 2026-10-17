"""Checked ctypes ABI for the single native host admission owner."""

import ctypes as C


class Config(C.Structure):
    _fields_ = [("abi_version", C.c_uint32), ("struct_size", C.c_uint32),
                ("key", C.c_uint8 * 32)] + [(name, C.c_uint64) for name in (
                    "run_id", "source_id", "session_id", "controller_id", "channel_id",
                    "map_epoch", "signing_seed", "map_start_us", "map_end_us",
                    "maximum_receipt_age_us", "maximum_confirmation_age_us")]


class Status(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in (
        "report_sequence", "transition_epoch", "validator_time_us")] + [
            (name, C.c_uint32) for name in (
                "frame_length", "status_class", "decision_code", "status_flags")] + [
                    ("payload", C.c_uint8 * 106), ("frame", C.c_uint8 * 280)]


class Result(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        "abi_version", "struct_size", "result_code", "code_category", "native_code",
        "parser_reason", "packet_kind", "signature_verified", "receipt_offered",
        "report_count", "terminal", "admission_closed", "has_eligible_request",
        "broker_code", "console_disposition", "event_consumed", "has_adapter_code",
        "adapter_code")] + [(name, C.c_uint64) for name in (
            "now_us", "completion_us", "received_us", "state_generation",
            "transition_epoch", "barrier_report_sequence", "barrier_time_us",
            "last_report_sequence", "ticket", "request_sequence", "request_created_us",
            "request_expiry_us")] + [("acceleration_frd_m_s2", C.c_float * 3),
                                     ("reserved", C.c_uint32), ("reports", Status * 8)]


def load_native(path):
    library = C.CDLL(str(path))
    ptr, result = C.c_void_p, C.POINTER(Result)
    signatures = {
        "c1h_config_size": (C.c_uint32, []), "c1h_result_size": (C.c_uint32, []),
        "c1h_now_ns": (C.c_uint64, []),
        "c1h_create": (C.c_int, [C.POINTER(Config), C.POINTER(ptr)]),
        "c1h_destroy": (None, [ptr]),
        "c1h_tick": (C.c_int, [ptr, C.c_uint32, result]),
        "c1h_receive": (C.c_int, [ptr, C.POINTER(C.c_uint8), C.c_uint32,
                                  C.c_uint64, C.c_uint64, result]),
        "c1h_access": (C.c_int, [ptr, result]),
        "c1h_operator_attach": (C.c_int, [ptr, C.c_uint64]),
        "c1h_operator_detach": (C.c_int, [ptr]),
        "c1h_operator_prepare": (C.c_int, [ptr, C.c_uint32, C.c_uint64, C.c_uint64, result]),
        "c1h_operator_confirm": (C.c_int, [ptr, C.c_uint64, result]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(library, name)
        function.restype, function.argtypes = restype, argtypes
    if library.c1h_config_size() != C.sizeof(Config) or library.c1h_result_size() != C.sizeof(Result):
        raise RuntimeError("C1H ABI size mismatch")
    return library


class NativeOwner:
    def __init__(self, library, config, clock):
        self.clock = clock
        if not clock.qualified or not clock.live_provider:
            raise RuntimeError("native owner requires qualified live clock")
        clock.check_environment()
        self.library, self.context = library, C.c_void_p()
        if library.c1h_create(C.byref(config), C.byref(self.context)) != 0 or not self.context:
            raise RuntimeError("native context rejected configuration")
        self.closed = False

    def close(self):
        if not self.closed:
            self.library.c1h_destroy(self.context)
            self.context = C.c_void_p()
            self.closed = True

    def call(self, name, *arguments):
        if self.closed:
            raise RuntimeError("native context closed")
        if name not in ("tick", "receive", "access", "operator_prepare", "operator_confirm"):
            raise RuntimeError("unsupported native operation")
        result = Result(abi_version=1, struct_size=C.sizeof(Result))
        try:
            self.clock.check_environment()
            pre = self.clock.now_ns()
            code = getattr(self.library, "c1h_" + name)(self.context, *arguments, C.byref(result))
            post = self.clock.now_ns()
            if code != 0 or result.result_code != 0:
                raise RuntimeError("native %s failed: %d/%d" % (name, code, result.result_code))
            if (result.abi_version != 1 or result.struct_size != C.sizeof(Result)
                    or result.report_count > 8 or result.reserved != 0):
                raise RuntimeError("invalid result ABI")
            if not pre // 1000 <= result.now_us <= result.completion_us <= post // 1000:
                raise RuntimeError("native operation escaped shared-clock bracket")
        except Exception:
            self.close()
            raise
        return result, pre, post

    def receive(self, data, received_ns, channel):
        raw = (C.c_uint8 * len(data)).from_buffer_copy(data)
        return self.call("receive", raw, len(data), received_ns // 1000, channel)

    def operator_lifecycle(self, name, *arguments):
        if self.closed or name not in ("attach", "detach"):
            raise RuntimeError("closed owner or unsupported operator lifecycle")
        try:
            self.clock.check_environment()
            if getattr(self.library, "c1h_operator_" + name)(self.context, *arguments) != 0:
                raise RuntimeError("operator " + name + " rejected")
        except Exception:
            self.close()
            raise


class OperatorDriver:
    """Independently scheduled test console; never passed to Guidance."""

    def __init__(self, owner, operator_id):
        self.owner = owner
        owner.operator_lifecycle("attach", operator_id)

    def prepare(self, intent, source=0, session=0):
        return self.owner.call("operator_prepare", intent, source, session)

    def confirm(self, ticket):
        return self.owner.call("operator_confirm", ticket)

    def detach(self):
        self.owner.operator_lifecycle("detach")
