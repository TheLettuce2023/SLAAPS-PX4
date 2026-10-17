"""Independent nominal C1H trace oracle. Missing evidence raises; measured misses fail.

This module evaluates supplied observations. It does not create measurements,
open links, or infer permission from absent status. Its unit fixtures are synthetic.
"""

from pathlib import Path
import argparse
import copy
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "wire"))
from codec import encode_request, encode_status
from host_clock import _validate_environment

COUNT = 20
PERIOD_NS = 20_000_000
UINT64_MAX = (1 << 64) - 1
BASE = {"run", "event"}
NATIVE = set("operation pre_ns post_ns now_us completion_us received_us parser_reason "
             "signature_verified packet_kind receipt_offered native_code category terminal closed eligible "
             "request_sequence request_created_us request_expiry_us acceleration reports "
             "last_admitted_us no_fresh".split())
SCHEMAS = {
    "native": NATIVE,
    "creation": {"actual_ns", "fields", "injected_fields"},
    "datagram": set("label send_pre_ns send_post_ns received_ns admission_pre_ns admission_post_ns bound byte_count".split()),
    "status_return": {"sent_ns", "returned_ns", "status"},
    "status_disposition": {"status_sequence", "disposition"},
    "operator_intent": {"intent", "at_ns"},
    "no_fresh_diagnostic": {"since_admission_us", "observed_us"},
    "nominal_schedule": set("releases_ns observer_us silence diagnostic_sent campaign_start_ns campaign_end_ns "
                            "request_count period_ns".split()),
    "run": {"manifest", "map", "identities", "sender", "receiver", "state_provenance", "key_policy"},
    "scenario_complete": {"at_ns", "result"},
}


def require(condition, reason):
    if not condition:
        raise ValueError("C1H incomplete/invalid evidence: " + reason)


def integer(value, label):
    require(type(value) is int and 0 <= value <= UINT64_MAX, label + " must be uint64")
    return value


def one(values, label):
    require(len(values) == 1, label + " must occur exactly once")
    return values[0]


def status_valid(status):
    require(type(status) is dict, "status must be a complete dictionary")
    encode_status(status)  # Frozen codec rejects extra, missing and ill-typed fields.


def _score(trace):
    require(type(trace) is list and bool(trace), "trace is empty")
    for row in trace:
        require(type(row) is dict and type(row.get("run")) is str and type(row.get("event")) is str,
                "row must identify run and event")
    rows = [row for row in trace if row["run"] == "nominal"]
    require(bool(rows), "nominal run absent")
    for row in rows:
        event = row["event"]
        require(event in SCHEMAS and set(row) == BASE | SCHEMAS[event], "unexpected/missing " + event + " fields")
        for name, value in row.items():
            if name.endswith(("_ns", "_us")) and name not in ("releases_ns", "observer_us"):
                integer(value, event + "." + name)
    run = one([row for row in rows if row["event"] == "run"], "run manifest")
    manifest = run["manifest"]
    require(type(manifest) is dict and manifest.get("qualified") is True
            and manifest.get("live_qualified") is True and manifest.get("provider_kind") == "live_default"
            and manifest.get("native_bracket_verified") is True, "live clock qualification absent")
    _validate_environment(manifest["environment"])
    for name in ("python_before_ns", "native_sample_ns", "python_after_ns"):
        integer(manifest[name], name)
    require(manifest["python_before_ns"] <= manifest["native_sample_ns"] <= manifest["python_after_ns"],
            "clock qualification bracket reversed")
    require(type(manifest.get("offset_us")) is int and manifest["offset_us"] == 0
            and type(manifest.get("uncertainty_us")) is int and manifest["uncertainty_us"] == 1,
            "clock mapping is not qualified identity/U1")
    interval = run["map"]
    require(type(interval) is dict and set(interval) == {"valid_from_us", "valid_until_us", "offset_us", "uncertainty_us"},
            "map fields")
    for name, value in interval.items():
        integer(value, "map." + name)
    require(interval["offset_us"] == 0 and interval["uncertainty_us"] == 1
            and interval["valid_from_us"] <= interval["valid_until_us"], "invalid map interval")
    identities = run["identities"]
    require(type(identities) is dict and set(identities) == {"run_id", "source_id", "session_id", "controller_id", "channel_id"},
            "identity fields")
    for name, value in identities.items():
        require(integer(value, name) > 0, "zero identity")
    require(run["state_provenance"] == "harness-simulated", "state provenance")
    for name in ("sender", "receiver"):
        endpoint = run[name]
        require(type(endpoint) in (list, tuple) and len(endpoint) == 2
                and endpoint[0] == "127.0.0.1" and type(endpoint[1]) is int
                and 1024 <= endpoint[1] <= 65535, "non-private/invalid " + name + " endpoint")
    require(run["sender"][1] != run["receiver"][1], "sender and receiver endpoints overlap")
    require(run["key_policy"] == "fresh ephemeral in-memory", "unapproved key lifecycle evidence")
    schedule = one([row for row in rows if row["event"] == "nominal_schedule"], "campaign schedule")
    require(type(schedule["request_count"]) is int and schedule["request_count"] == COUNT
            and schedule["period_ns"] == PERIOD_NS, "campaign count/period differs from declared profile")
    start, end = schedule["campaign_start_ns"], schedule["campaign_end_ns"]
    require(start < end, "campaign interval empty")
    releases = schedule["releases_ns"]
    require(type(releases) is list and len(releases) == COUNT, "twenty release observations required")
    for value in releases:
        integer(value, "release")
    require(all(start <= value <= end for value in releases)
            and all(a < b for a, b in zip(releases, releases[1:])), "release chronology")
    require(type(schedule["observer_us"]) is list and bool(schedule["observer_us"]), "explicit observer evidence absent")
    for value in schedule["observer_us"]:
        integer(value, "observer_us")

    native = [row for row in rows if row["event"] == "native"]
    require(bool(native), "native observations absent")
    generated = {}
    generated_post = {}
    prior_post = 0
    prior_report = 0
    for row in native:
        for name in NATIVE - {"operation", "acceleration", "reports", "no_fresh"}:
            integer(row[name], "native." + name)
        require(type(row["no_fresh"]) is bool, "no_fresh must be Boolean")
        require(type(row["operation"]) is str and type(row["reports"]) is list and 1 <= len(row["reports"]) <= 8,
                "native operation/report capacity")
        require(row["pre_ns"] >= prior_post and row["pre_ns"] <= row["post_ns"], "native call chronology")
        require(row["pre_ns"] // 1000 <= row["now_us"] <= row["completion_us"] <= row["post_ns"] // 1000,
                "native sample outside Python bracket")
        prior_post = row["post_ns"]
        require(type(row["acceleration"]) is list and len(row["acceleration"]) == 3, "access vector absent")
        for name in ("signature_verified", "receipt_offered", "terminal", "closed", "eligible"):
            require(row[name] in (0, 1), "native Boolean flag")
        for status in row["reports"]:
            status_valid(status)
            sequence = status["status_sequence"]
            require(sequence == prior_report + 1, "missing/repeated/reordered native report")
            prior_report = sequence
            require(row["pre_ns"] // 1000 <= status["validator_time_us"] <= row["now_us"], "report sample outside operation")
            require(status["controller_instance_id"] == identities["controller_id"], "report controller mismatch")
            generated[sequence], generated_post[sequence] = status, row["post_ns"]
    dispositions = {}
    for row in rows:
        if row["event"] == "status_disposition":
            sequence = integer(row["status_sequence"], "status disposition sequence")
            require(sequence in generated and sequence not in dispositions, "unattributed/repeated status disposition")
            require(row["disposition"] in ("enqueued", "local_observation"), "nominal status unexpectedly lost")
            dispositions[sequence] = row["disposition"]
    require(set(dispositions) == set(generated), "missing status disposition")
    returned = {}
    snapshot_sends = []
    for row in rows:
        if row["event"] != "status_return":
            continue
        status_valid(row["status"])
        sequence = row["status"]["status_sequence"]
        require(sequence in generated and sequence not in returned and dispositions[sequence] == "enqueued",
                "unattributed/repeated status return")
        require(row["status"] == generated[sequence], "returned payload differs from historical report")
        require(generated_post[sequence] <= row["sent_ns"] <= row["returned_ns"], "status return chronology")
        returned[sequence] = row
        if row["status"]["status_class"] == 5:
            snapshot_sends.append(row["sent_ns"])
    require(set(returned) == {seq for seq, disposition in dispositions.items() if disposition == "enqueued"},
            "enqueued status has no return")

    campaign = [row for row in native if start <= row["pre_ns"] <= end]
    require(bool(campaign) and all(row["post_ns"] <= end for row in campaign), "native campaign endpoints incomplete")
    creations = [row for row in rows if row["event"] == "creation" and start <= row["actual_ns"] <= end]
    datagrams = [row for row in rows if row["event"] == "datagram" and row["label"] == "request"
                 and start <= row["send_pre_ns"] <= end]
    admitted = [row for row in campaign if row["operation"] == "receive:request"]
    require(len(creations) == len(datagrams) == len(admitted) == COUNT, "campaign request evidence count")
    misses = []
    measurements = {}

    def measure(criterion, actual, maximum=None):
        entry = measurements.setdefault(criterion, {"samples": 0, "minimum": actual, "maximum": actual})
        entry["samples"] += 1
        entry["minimum"] = min(entry["minimum"], actual)
        entry["maximum"] = max(entry["maximum"], actual)
        if maximum is not None:
            entry["limit"] = maximum

    def limit(criterion, actual, maximum, **extra):
        measure(criterion, actual, maximum)
        if actual > maximum:
            misses.append(dict(criterion=criterion, measured=actual, limit=maximum, **extra))

    for index, (left, right) in enumerate(zip(releases, releases[1:])):
        limit("release_gap_ns", right - left, 24_000_000, index=index)
    for index, release in enumerate(releases):
        require(release >= start + index * PERIOD_NS, "release preceded fixed nominal schedule")
        measure("release_lateness_ns", release - (start + index * PERIOD_NS))
    for row in returned.values():
        measure("status_report_to_return_ns", row["returned_ns"] - row["status"]["validator_time_us"] * 1000)
    for left, right in zip(snapshot_sends, snapshot_sends[1:]):
        measure("diagnostic_snapshot_spacing_ns", right-left)
        if right - left < 1_000_000_000:
            misses.append(dict(criterion="diagnostic_snapshot_spacing_ns", measured=right-left, minimum=1_000_000_000))
    observed = [status["validator_time_us"] for row in campaign for status in row["reports"]]
    require(all(a <= b for a, b in zip(observed, observed[1:])), "observer clock regressed")
    tick_times = [row["now_us"] for row in campaign if row["operation"] == "tick"
                  and row["pre_ns"] < admitted[-1]["pre_ns"]]
    require(schedule["observer_us"] == tick_times, "periodic observer inventory incomplete/mismatched")
    endpoints = [start] + [value * 1000 for value in observed if value * 1000 >= start] + [end]
    require(len(endpoints) > 2, "observer coverage absent")
    for index, (left, right) in enumerate(zip(endpoints, endpoints[1:])):
        limit("observer_gap_ns", right-left, 2_000_000, index=index)

    request_by_sequence = {}
    for index, (creation, datagram, admission) in enumerate(zip(creations, datagrams, admitted)):
        require(creation["injected_fields"] == [], "nominal request timestamp/fields injected")
        require(type(creation["fields"]) is dict and "reserved" not in creation["fields"],
                "recorded request fields must omit reserved bytes; do not overwrite evidence")
        fields = dict(creation["fields"])
        fields["reserved"] = bytes(4)
        encode_request(fields)
        sequence = fields["sequence"]
        require(sequence not in request_by_sequence, "nominal command sequence repeated")
        request_by_sequence[sequence] = fields
        created, expiry = fields["producer_monotonic_us"], fields["producer_expiry_monotonic_us"]
        require(created == creation["actual_ns"] // 1000 and expiry - created == 80_000,
                "creation/expiry not immutable measured 80ms interval")
        require(interval["valid_from_us"] <= created < expiry <= interval["valid_until_us"], "map lifetime coverage")
        require(releases[index] <= creation["actual_ns"] <= datagram["send_pre_ns"] <= datagram["send_post_ns"]
                <= datagram["received_ns"] <= datagram["admission_pre_ns"] <= datagram["admission_post_ns"],
                "request lifecycle timestamp chronology")
        require(type(datagram["bound"]) is bool and datagram["bound"] and 110 <= integer(datagram["byte_count"], "frame size") <= 115,
                "request peer/size evidence")
        require((admission["pre_ns"], admission["post_ns"], admission["received_us"]) ==
                (datagram["admission_pre_ns"], datagram["admission_post_ns"], datagram["received_ns"] // 1000),
                "datagram/admission mismatch")
        require(admission["category"] == 2 and admission["native_code"] == 1
                and admission["parser_reason"] == 0 and admission["signature_verified"] == 1
                and admission["packet_kind"] == 1 and not admission["closed"] and not admission["terminal"],
                "nominal request not actually authenticated/admitted")
        status = one([status for status in admission["reports"] if status["status_class"] == 1], "correlated request decision")
        require(status["decision_code"] == 1 and status["status_flags"] == 59
                and status["correlation_sequence"] == status["last_accepted_sequence"] == sequence
                and status["source_id"] == fields["source_id"] == identities["source_id"]
                and status["session_id"] == fields["session_id"] == identities["session_id"]
                and status["controller_instance_id"] == fields["controller_instance_id"]
                and status["transition_epoch"] == fields["transition_epoch"]
                and status["clock_mapping_epoch"] == fields["clock_mapping_epoch"]
                and status["conservative_age_us"] == status["validator_time_us"] - created + 1,
                "accepted request status correlation/evidence mismatch")
        require(status["status_sequence"] in returned, "accepted request decision not returned")
        position = campaign.index(admission)
        require(position + 1 < len(campaign) and campaign[position + 1]["operation"] == "access", "request access evidence absent")
        access = campaign[position + 1]
        require(access["eligible"] == 1 and access["request_sequence"] == sequence
                and access["request_created_us"] == created and access["request_expiry_us"] == expiry
                and access["acceleration"] == [fields["acceleration_x_frd_m_s2"], fields["acceleration_y_frd_m_s2"],
                                                fields["acceleration_z_frd_m_s2"]], "full canonical request access mismatch")
        limit("created_to_received_ns", datagram["received_ns"] - creation["actual_ns"], 12_000_000, sequence=sequence)
        measure("created_to_send_ns", datagram["send_pre_ns"] - creation["actual_ns"])
        limit("received_to_admission_completion_ns", admission["post_ns"] - datagram["received_ns"], 4_000_000, sequence=sequence)
        limit("conservative_admission_completion_age_ns", admission["post_ns"] - (created - 1) * 1000,
              20_000_000, sequence=sequence)
    last_admitted = 0
    for row in campaign:
        if row["operation"] == "receive:request":
            last_admitted = row["now_us"]
        require(row["last_admitted_us"] == last_admitted, "diagnostic admission history altered")
        require(row["no_fresh"] == (last_admitted != 0 and row["now_us"] - last_admitted >= 40_000),
                "no-fresh diagnostic threshold mismatch")
        if row["operation"] == "access" and row["eligible"]:
            require(row["request_sequence"] in request_by_sequence, "eligible access has no admitted request")
            original = request_by_sequence[row["request_sequence"]]
            require(row["request_created_us"] == original["producer_monotonic_us"]
                    and row["request_expiry_us"] == original["producer_expiry_monotonic_us"], "retained request times changed")
            limit("access_completion_hard_age_ns", row["post_ns"] - (row["request_created_us"] - 1) * 1000, 60_000_000)
            if row["post_ns"] >= (row["request_expiry_us"] - 1) * 1000:
                misses.append(dict(criterion="access_completion_exclusive_expiry", measured=row["post_ns"],
                                   exclusive_limit=(row["request_expiry_us"] - 1) * 1000))
    diagnostic = one([row for row in rows if row["event"] == "no_fresh_diagnostic"], "40ms diagnostic")
    require(type(schedule["diagnostic_sent"]) is bool and schedule["diagnostic_sent"], "diagnostic evidence absent")
    after_last = [row for row in campaign if row["operation"] == "access" and row["now_us"] >= last_admitted + 40_000]
    require(bool(after_last) and diagnostic["since_admission_us"] == last_admitted
            and diagnostic["observed_us"] == after_last[0]["now_us"], "diagnostic not based on first observed 40ms threshold")
    threshold_position = campaign.index(after_last[0])
    require(threshold_position + 1 < len(campaign) and campaign[threshold_position + 1]["operation"] == "tick",
            "40ms diagnostic has no following real snapshot tick")
    diagnostic_tick = campaign[threshold_position + 1]
    diagnostic_status = one([status for status in diagnostic_tick["reports"] if status["status_class"] == 5],
                            "40ms diagnostic snapshot report")
    require(diagnostic_status["validator_time_us"] >= diagnostic["observed_us"]
            and diagnostic_status["status_sequence"] in returned,
            "40ms diagnostic snapshot was not actually returned")
    limit("diagnostic_observation_delay_us", diagnostic["observed_us"] - last_admitted - 40_000, 2000)
    require(type(schedule["silence"]) is list and bool(schedule["silence"]), "silence observations absent")
    silence = schedule["silence"]
    for pair in silence:
        require(type(pair) in (list, tuple) and len(pair) == 2 and type(pair[1]) is bool, "silence row shape")
        integer(pair[0], "silence sample")
    actual_silence = [(row["now_us"], bool(row["eligible"])) for row in campaign if row["operation"] == "access"
                      and row["now_us"] >= silence[0][0]]
    require([tuple(pair) for pair in silence] == actual_silence, "silence access inventory incomplete")
    require(end // 1000 >= last_admitted + 100_000 and any(not pair[1] for pair in silence), "silence interval incomplete")
    return {"timing_result": "FAIL" if misses else "PASS", "misses": misses,
            "scope": "nominal_campaign_only", "functional_scenarios": "validated_by_runner_not_this_oracle",
            "measurements": measurements,
            "requests_verified": COUNT, "native_observations_verified": len(campaign),
            "status_returns_verified": len(returned), "evidence_result": "COMPLETE",
            "limits": "Predeclared C1H host timeline targets; no physical timing claim"}


def score(trace):
    """Return measured misses, or raise ValueError when evidence is incomplete."""
    try:
        return _score(trace)
    except (KeyError, TypeError, IndexError, OverflowError) as error:
        raise ValueError("C1H incomplete/invalid evidence structure") from error


def mutation_check(trace):
    """Corrupt copies of a complete artifact; never change the original evidence."""
    score(trace)  # A complete artifact may honestly contain timing misses.
    mutations = []
    for event in ("creation", "datagram", "status_return", "status_disposition", "native", "no_fresh_diagnostic"):
        changed = copy.deepcopy(trace)
        schedule = next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "nominal_schedule")
        positions = [index for index, row in enumerate(changed) if row.get("run") == "nominal" and row.get("event") == event]
        if event == "creation":
            positions = [index for index in positions if changed[index]["actual_ns"] >= schedule["campaign_start_ns"]]
        elif event == "datagram":
            positions = [index for index in positions if changed[index]["label"] == "request"
                         and changed[index]["send_pre_ns"] >= schedule["campaign_start_ns"]]
        del changed[positions[0]]
        mutations.append(changed)
    for name, value in (("actual_ns", None), ("actual_ns", True), ("actual_ns", -1), ("extra_timestamp_ns", 1)):
        changed = copy.deepcopy(trace)
        next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "creation")[name] = value
        mutations.append(changed)
    changed = copy.deepcopy(trace)
    next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "status_return")["status"]["correlation_sequence"] += 1
    mutations.append(changed)
    changed = copy.deepcopy(trace)
    next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "nominal_schedule")["observer_us"] = []
    mutations.append(changed)
    for name, value in (("sender", ["0.0.0.0", 14550]), ("receiver", ["192.0.2.1", 14550]),
                        ("sender", ["127.0.0.1", 0]), ("key_policy", "reused hardware key")):
        changed = copy.deepcopy(trace)
        next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "run")[name] = value
        mutations.append(changed)
    changed = copy.deepcopy(trace)
    run = next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "run")
    run["receiver"] = copy.deepcopy(run["sender"])
    mutations.append(changed)
    changed = copy.deepcopy(trace)
    schedule = next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "nominal_schedule")
    schedule["observer_us"] = schedule["observer_us"][:1]
    mutations.append(changed)
    changed = copy.deepcopy(trace)
    schedule = next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "nominal_schedule")
    next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "creation"
         and row["actual_ns"] >= schedule["campaign_start_ns"])["fields"]["reserved"] = "discarded malicious bytes"
    mutations.append(changed)
    changed = copy.deepcopy(trace)
    diagnostic_return = next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "status_return"
                             and row["status"]["status_class"] == 5)
    sequence = diagnostic_return["status"]["status_sequence"]
    changed.remove(diagnostic_return)
    next(row for row in changed if row.get("run") == "nominal" and row.get("event") == "status_disposition"
         and row["status_sequence"] == sequence)["disposition"] = "local_observation"
    mutations.append(changed)
    for index, changed in enumerate(mutations):
        try:
            score(changed)
        except ValueError:
            continue
        raise ValueError("C1H checker accepted deliberately corrupted evidence mutation " + str(index))
    return len(mutations)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON evidence key: " + key)
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--mutations", action="store_true", help="reject corrupted copies without changing supplied evidence")
    arguments = parser.parse_args()
    with arguments.trace.open(encoding="utf-8") as source:
        trace = json.load(source, object_pairs_hook=_unique_object)
    result = score(trace)
    if arguments.mutations:
        result["mutation_checks"] = mutation_check(trace)
    print(json.dumps(result, sort_keys=True))
    if result["timing_result"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
