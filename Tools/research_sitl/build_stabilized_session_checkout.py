#!/usr/bin/env python3
"""Fresh private PX4 link with the session consumer; does not qualify runtime."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import build_stabilized_manual_checkout as builder

ROOT = builder.base.ROOT

def build(out, jobs):
    record = json.loads((out / "checkout-source-origin.json").read_text())
    builder.verify_sources(out, record)
    source = out / "px4-source-publisher-epoch"
    native, plugin = out / "sitl-lockstep-build", out / "session-plugin-build"
    env = dict(__import__("os").environ, GIT_SUBMODULES_ARE_EVIL="1")
    builder.base.run(["cmake", "-S", str(source / "Tools/research_sitl/stabilized_session"),
                      "-B", str(plugin), "-G", "Ninja", "-DBUILD_SESSION_PLUGIN=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
                     out, "session-plugin-configure.log", env=env)
    builder.base.run(["cmake", "--build", str(plugin), "-j", str(jobs)], out, "session-plugin-build.log", env=env)
    builder.base.run(["cmake", "-S", str(source), "-B", str(native), "-G", "Ninja", "-DCONFIG=px4_sitl_default",
                      "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_CXX_FLAGS=",
                      "-DEXTERNAL_MODULES_LOCATION=" + str(source / "Tools/research_sitl/stabilized_manual/px4_external")],
                     out, "session-sitl-configure.log", env=env)
    builder.base.run(["cmake", "--build", str(native), "--target", "mavlink_c_generate", "-j", str(jobs)],
                     out, "session-sitl-mavlink.log", env=env)
    builder.base.run(["cmake", "--build", str(native), "-j", str(jobs)], out, "session-sitl-build.log", env=env)
    builder.verify_sources(out, record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-only", type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    assert args.jobs > 0
    if args.build_only:
        out = args.build_only.resolve()
        assert out.parent == Path("/var/tmp") and out.name.startswith("research-stabilized-sitl-")
        if (out / "session-link-manifest.json").exists():
            raise RuntimeError("Sealed session-link evidence is immutable")
    else:
        out = Path(tempfile.mkdtemp(prefix="research-stabilized-sitl-", dir="/var/tmp"))
        print("Fresh session checkout:", out, flush=True)
        builder.OVERLAYS += ("Tools/research_sitl/stabilized_session", "src/modules/research_stabilized_manual")
        record = builder.prepare(out)
        source = out / "px4-source-publisher-epoch"
        patches = {}
        board = "boards/px4/sitl/default.px4board"
        text = (source / board).read_text()
        text = "\n".join(line for line in text.splitlines() if not line.startswith((
            "CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL=", "CONFIG_MODULES_RESEARCH_BENCH_CONTROL=")))
        (source / board).write_text(text + "\nCONFIG_MODULES_RESEARCH_STABILIZED_MANUAL=y\nCONFIG_MODULES_RESEARCH_BENCH_CONTROL=n\n")
        patches[board] = builder.base.digest(source / board)
        external = "Tools/research_sitl/stabilized_manual/px4_external/src/CMakeLists.txt"
        with (source / external).open("a") as stream:
            stream.write('\nadd_subdirectory("${PX4_SOURCE_DIR}/Tools/research_sitl/stabilized_session/px4_external/src/research_session_consumer" "${CMAKE_CURRENT_BINARY_DIR}/research_session_consumer")\n')
        patches[external] = builder.base.digest(source / external)
        messages = "msg/CMakeLists.txt"
        text = (source / messages).read_text()
        anchor = "\tStabilizedObserverStatus.msg\n"
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + "\tStabilizedOutputCommand.msg\n\tStabilizedOutputAck.msg\n")
        (source / messages).write_text(text)
        patches[messages] = builder.base.digest(source / messages)
        donors = {}
        for name in ("StabilizedOutputCommand.msg", "StabilizedOutputAck.msg"):
            donor = ROOT / "Tools/research_sitl/stabilized_output" / name
            destination = "msg/" + name
            shutil.copy2(donor, source / destination)
            identity = builder.base.digest(donor)
            record["working_inputs"][destination] = dict(kind="file", sha256=identity, mode="100644")
            donors[str(donor.relative_to(ROOT))] = dict(destination=destination, sha256=identity)
        record["patched_sources"].update(patches)
        record["session_consumer_private_patches"] = patches
        record["session_message_donors"] = donors
        record["scope"] = "current_checkout_private_POSIX_session_consumer_link"
        (out / "checkout-source-origin.json").write_text(json.dumps(record, indent=2) + "\n")
        shutil.copy2(__file__, out / Path(__file__).name)
    # The legacy dynamic instrumentation and observer share field names and
    # are alternative implementations. This new executable uses the current
    # observer implementation only; do not compile the legacy fixture owner.
    source = out / "px4-source-publisher-epoch"
    record = json.loads((out / "checkout-source-origin.json").read_text())
    revision = Path(tempfile.mkdtemp(prefix="session-source-revision-", dir=out))
    shutil.copy2(out / "checkout-source-origin.json", revision / "origin.json")
    for name in ("platforms/common/uORB/uORBDeviceNode.hpp", "platforms/common/uORB/uORBDeviceNode.cpp"):
        backup = revision / name
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, backup)
        shutil.copy2(ROOT / name, source / name)
        record["patched_sources"][name] = builder.base.digest(source / name)
    external = "Tools/research_sitl/stabilized_manual/px4_external/src/CMakeLists.txt"
    backup = revision / external
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / external, backup)
    text = (source / external).read_text().replace("add_subdirectory(research_stabilized_sitl)\n", "")
    (source / external).write_text(text)
    record["patched_sources"][external] = builder.base.digest(source / external)
    record["session_consumer_private_patches"] = record["patched_sources"].copy()
    (out / "checkout-source-origin.json").write_text(json.dumps(record, indent=2) + "\n")
    shutil.copy2(__file__, out / Path(__file__).name)
    build(out, args.jobs)
    source = out / "px4-source-publisher-epoch"
    record = json.loads((out / "checkout-source-origin.json").read_text())
    for name, item in record["session_message_donors"].items():
        assert builder.base.digest(ROOT / name) == item["sha256"] == builder.base.digest(source / item["destination"])
    binary = out / "sitl-lockstep-build/bin/px4"
    symbols = subprocess.check_output(["nm", "-C", str(binary)], text=True)
    for required in ("research_session_consumer_main", "Px4SessionConsumer::Run()", "research_session_sitl_main", "Px4SessionSITL::Run()", "StabilizedManualSession::update", "SelectedStateGuard::", "StabilizedOutputDriver::complete"):
        assert required in symbols, required
    (out / "session-linked-symbols.txt").write_text("\n".join(line for line in symbols.splitlines() if "SessionConsumer" in line or "session_consumer" in line))
    receipt = dict(scope="fresh_current_checkout_PX4_session_producer_consumer_link_only", passed=True,
                   px4_sha256=builder.base.digest(binary), staged_inputs=len(record["working_inputs"]),
                   preparation_anchors=record["preparation_anchors"], runtime_verified=False,
                   closed_loop_qualified=False, hardware_operated=False)
    (out / "session-link-result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("SESSION LINK PASS", json.dumps(receipt), flush=True)

if __name__ == "__main__":
    main()
