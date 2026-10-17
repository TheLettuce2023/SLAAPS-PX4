"""Synthetic checker fixtures only. These tests make no live measurement claim."""

import copy
import unittest

from check_live_evidence import mutation_check, score, _unique_object


def synthetic_trace():
    # Intentionally constructed evidence to test the oracle, never a run artifact.
    start = 1_000_000_000
    rows = []

    def emit(event, **values):
        row = dict(run="nominal", event=event, **values)
        rows.append(row)
        return row

    environment = dict(python_implementation="cpython", python_version=[3, 12, 3], platform="linux",
        kernel_release="synthetic-microsoft-fixture", distribution="ubuntu", distribution_version="24.04",
        clock_implementation="clock_gettime(CLOCK_MONOTONIC)", monotonic=True, adjustable=False,
        resolution_seconds=1e-9, clock_getres_seconds=1e-9, boot_id="synthetic-checker-only",
        time_namespace="time:[123]", time_namespace_offsets="monotonic 0 0\nboottime 0 0",
        clocksource="fixture", process_id=1)
    emit("run", manifest=dict(qualified=True, live_qualified=True, provider_kind="live_default",
         native_bracket_verified=True, environment=environment, python_before_ns=start-10000,
         native_sample_ns=start-9000, python_after_ns=start-8000, offset_us=0, uncertainty_us=1),
         map=dict(valid_from_us=0, valid_until_us=2_000_000, offset_us=0, uncertainty_us=1),
         identities=dict(run_id=1, source_id=2, session_id=3, controller_id=4, channel_id=5),
         sender=["127.0.0.1", 1234], receiver=["127.0.0.1", 1235],
         state_provenance="harness-simulated", key_policy="fresh ephemeral in-memory")
    report_sequence = 0
    accepted = None
    last_admitted = 0
    releases, ticks, silence = [], [], []
    diagnostic = False

    def native(operation, pre, *, request=False, received=0, snapshot=False):
        nonlocal report_sequence, last_admitted
        now, post = pre // 1000 + 2, pre + 4000
        if request:
            last_admitted = now
        eligible = accepted is not None and now - accepted["producer_monotonic_us"] + 1 <= 60_000
        report_sequence += 1
        status = dict(controller_instance_id=4, transition_epoch=3, clock_mapping_epoch=1,
            source_id=2, session_id=3, correlation_sequence=accepted["sequence"] if request else 0,
            last_accepted_sequence=accepted["sequence"] if accepted else 0, validator_time_us=now,
            status_sequence=report_sequence, rejection_count=0,
            conservative_age_us=now-accepted["producer_monotonic_us"]+1 if eligible else 0,
            protocol_version=65536, decision_code=1 if eligible else 36, first_latched_code=0,
            status_flags=59 if eligible else 16 if accepted else 0,
            target_system=42, target_component=191, status_class=1 if request else 5,
            arm_state=2, research_mode_state=2, session_state=2, transport_state=1, clock_mapping_state=1)
        copied = eligible and operation == "access"
        row = emit("native", operation=operation, pre_ns=pre, post_ns=post, now_us=now, completion_us=post//1000,
            received_us=received//1000, parser_reason=0 if request else 1, signature_verified=int(request),
            packet_kind=int(request), receipt_offered=0, native_code=1 if request else 6 if copied else 1,
            category=2 if request else 1, terminal=0, closed=0, eligible=int(copied),
            request_sequence=accepted["sequence"] if copied else 0,
            request_created_us=accepted["producer_monotonic_us"] if copied else 0,
            request_expiry_us=accepted["producer_expiry_monotonic_us"] if copied else 0,
            acceleration=[1.0, -0.5, 0.25] if copied else [0.0, 0.0, 0.0], reports=[status],
            last_admitted_us=last_admitted, no_fresh=last_admitted != 0 and now-last_admitted >= 40_000)
        emit("status_disposition", status_sequence=report_sequence,
             disposition="enqueued" if request or snapshot else "local_observation")
        if request or snapshot:
            emit("status_return", sent_ns=post+10, returned_ns=post+20, status=copy.deepcopy(status))
        return row

    for millisecond in range(482):
        moment = start + millisecond * 1_000_000
        if millisecond % 20 == 0 and millisecond <= 380:
            sequence = millisecond // 20 + 1
            releases.append(moment)
            created = moment + 10000
            accepted = dict(source_id=2, session_id=3, sequence=sequence,
                producer_monotonic_us=created//1000, producer_expiry_monotonic_us=created//1000+80000,
                controller_instance_id=4, transition_epoch=3, clock_mapping_epoch=1,
                acceleration_x_frd_m_s2=1.0, acceleration_y_frd_m_s2=-0.5, acceleration_z_frd_m_s2=0.25,
                protocol_version=65536, target_system=1, target_component=1, frame=1, quantity=1,
                reference_point=1, flags=0)
            emit("creation", actual_ns=created, fields=copy.deepcopy(accepted), injected_fields=[])
            received, admission_pre = moment+30000, moment+40000
            emit("datagram", label="request", send_pre_ns=moment+20000, send_post_ns=moment+22000,
                received_ns=received, admission_pre_ns=admission_pre, admission_post_ns=admission_pre+4000,
                bound=True, byte_count=115)
            native("receive:request", admission_pre, request=True, received=received)
            native("access", moment+70000)
        elif millisecond <= 380:
            ticks.append(native("tick", moment)["now_us"])
        else:
            result = native("access", moment)
            silence.append([result["now_us"], bool(result["eligible"])])
            if result["no_fresh"] and not diagnostic:
                native("tick", moment+10000, snapshot=True)
                emit("no_fresh_diagnostic", since_admission_us=last_admitted, observed_us=result["now_us"])
                diagnostic = True
    emit("nominal_schedule", releases_ns=releases, observer_us=ticks, silence=silence,
         diagnostic_sent=diagnostic, campaign_start_ns=start, campaign_end_ns=start+482_000_000,
         request_count=20, period_ns=20_000_000)
    return rows


class EvidenceTests(unittest.TestCase):
    def test_complete_synthetic_fixture(self):
        result = score(synthetic_trace())
        self.assertEqual(result["timing_result"], "PASS")
        self.assertEqual(result["requests_verified"], 20)
        self.assertEqual(result["status_returns_verified"], 21)
        self.assertEqual(result["measurements"]["created_to_received_ns"]["maximum"], 20000)
        self.assertEqual(mutation_check(synthetic_trace()), 20)
        self.assertEqual(result["scope"], "nominal_campaign_only")

    def test_private_endpoints_and_key_policy(self):
        for name, value in (("sender", ["0.0.0.0", 14550]), ("receiver", ["192.0.2.1", 14550]),
                            ("sender", ["127.0.0.1", True]), ("sender", ["127.0.0.1", 65536]),
                            ("key_policy", "synthetic checker fixture")):
            with self.subTest(name=name, value=value):
                rows = synthetic_trace()
                rows[0][name] = value
                with self.assertRaises(ValueError):
                    score(rows)

    def test_observer_prefix_and_overwritten_reserved_refused(self):
        for mutation in ("observer_prefix", "reserved"):
            rows = synthetic_trace()
            if mutation == "observer_prefix":
                schedule = next(row for row in rows if row["event"] == "nominal_schedule")
                schedule["observer_us"] = schedule["observer_us"][:1]
            else:
                next(row for row in rows if row["event"] == "creation")["fields"]["reserved"] = "not zero"
            with self.assertRaises(ValueError):
                score(rows)

    def test_local_diagnostic_does_not_prove_return(self):
        rows = synthetic_trace()
        returned = next(row for row in rows if row["event"] == "status_return" and row["status"]["status_class"] == 5)
        sequence = returned["status"]["status_sequence"]
        rows.remove(returned)
        next(row for row in rows if row["event"] == "status_disposition" and row["status_sequence"] == sequence)["disposition"] = "local_observation"
        with self.assertRaises(ValueError):
            score(rows)

    def test_duplicate_json_keys_rejected(self):
        with self.assertRaises(ValueError):
            _unique_object([("timestamp", 1), ("timestamp", 2)])

    def test_missing_required_evidence_rejected(self):
        for event in ("creation", "datagram", "status_return", "status_disposition", "no_fresh_diagnostic", "native"):
            with self.subTest(event=event):
                rows = synthetic_trace()
                del rows[next(index for index, row in enumerate(rows) if row["event"] == event)]
                with self.assertRaises(ValueError):
                    score(rows)

    def test_observer_gap_cannot_hide_in_missing_row(self):
        rows = synthetic_trace()
        del rows[next(index for index, row in enumerate(rows) if row["event"] == "native" and row["operation"] == "tick")]
        with self.assertRaises(ValueError):
            score(rows)

    def test_illtyped_extra_and_reversed_timestamps(self):
        for field, value in (("actual_ns", True), ("actual_ns", -1), ("actual_ns", None), ("bogus_ns", 123)):
            with self.subTest(field=field, value=value):
                rows = synthetic_trace()
                next(row for row in rows if row["event"] == "creation")[field] = value
                with self.assertRaises(ValueError):
                    score(rows)

    def test_return_payload_and_accessor_time_mutations(self):
        for event in ("status_return", "native"):
            rows = synthetic_trace()
            if event == "status_return":
                next(row for row in rows if row["event"] == event)["status"]["correlation_sequence"] += 1
            else:
                next(row for row in rows if row["event"] == event and row["operation"] == "access")["request_expiry_us"] += 1
            with self.assertRaises(ValueError):
                score(rows)

    def test_early_diagnostic_rejected(self):
        rows = synthetic_trace()
        diagnostic = next(row for row in rows if row["event"] == "no_fresh_diagnostic")
        diagnostic["observed_us"] = diagnostic["since_admission_us"] + 39999
        with self.assertRaises(ValueError):
            score(rows)

    def test_incomplete_clock_and_empty_counts_rejected(self):
        for mutation in ("clock", "count", "observer"):
            rows = synthetic_trace()
            if mutation == "clock":
                rows[0]["manifest"]["native_sample_ns"] = None
            else:
                schedule = next(row for row in rows if row["event"] == "nominal_schedule")
                schedule["releases_ns" if mutation == "count" else "observer_us"] = []
            with self.assertRaises(ValueError):
                score(rows)

    def test_measured_miss_differs_from_incomplete_evidence(self):
        rows = synthetic_trace()
        schedule = next(row for row in rows if row["event"] == "nominal_schedule")
        schedule["campaign_start_ns"] -= 5_000_000
        result = score(rows)
        self.assertEqual(result["timing_result"], "FAIL")
        self.assertEqual(result["evidence_result"], "COMPLETE")
        self.assertTrue(any(miss["criterion"] == "observer_gap_ns" for miss in result["misses"]))


if __name__ == "__main__":
    unittest.main()
