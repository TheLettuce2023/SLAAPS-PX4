#!/usr/bin/env python3
"""Compile active SLAAPS interfaces with retained board flags; no link or hardware.

Run inside px4-dev with the board volume at /work (read-only), current checkout
at /workspace (read-only), and a fresh writable /output evidence directory.
"""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import struct


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root, out = Path("/workspace"), Path("/output")
    assert not (out / "result.json").exists()
    assert not (out / "overlay").exists(), "Use a fresh probe stage"
    build = Path("/work/source/build/px4_fmu-v6x_slaaps")
    commands = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in commands if item["file"].endswith("/src/modules/slaaps/Slaaps.cpp"))
    hashes = {}
    paths = (list((root / "src/lib/slaaps").glob("*"))
             + list((root / "src/lib/research_stabilized_manual").glob("*.hpp"))) + [root / "Tools/slaaps/active_target/ActiveTransportCompile.cpp", Path(__file__)]
    for path in paths:
        if not path.is_file():
            continue
        name = path.relative_to(root)
        target = out / "overlay" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        hashes[name.as_posix()] = digest(path)
    command = shlex.split(entry["command"])
    index = command.index("-o")
    command[index + 1] = str(out / "active.o")
    command[command.index("-c") + 1] = str(out / "overlay/Tools/slaaps/active_target/ActiveTransportCompile.cpp")
    command[1:1] = ["-I" + str(out / "overlay/src"), "-MD", "-MF", str(out / "active.d"), "-fstack-usage"]
    (out / "command.json").write_text(json.dumps(command, indent=2))
    (out / "source-hashes.json").write_text(json.dumps(hashes, indent=2))
    (out / "compiler.txt").write_bytes(subprocess.check_output([command[0], "--version"]))
    result = subprocess.run(command, cwd=build, capture_output=True, text=True)
    (out / "compile.log").write_text(result.stdout + result.stderr)
    assert result.returncode == 0, f"Target compile failed: {out}/compile.log"
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(out / "active.o")], text=True)
    for symbol in ("ManualBankTransport<0u>::prepare", "ManualBankTransport<1u>::beginPrepared", "slaaps_target_selected_begin", "slaaps_target_selected_cycle", "slaaps_target_begin", "slaaps_target_cycle", "slaaps_target_destroy", "slaaps_target_main_create", "slaaps_target_aux_create", "slaaps_target_main_destroy", "slaaps_target_aux_destroy", "ManualBankTransport<0u>::confirmHandled", "ManualBankTransport<1u>::dispatch"):
        assert symbol in symbols, symbol
    (out / "symbols.txt").write_text(symbols)
    (out / "size.txt").write_bytes(subprocess.check_output(["arm-none-eabi-size", str(out / "active.o")]))
    subprocess.run(["arm-none-eabi-objcopy", "--dump-section", ".rodata.slaaps_target_sizes=" + str(out / "sizes.bin"),
                    str(out / "active.o"), str(out / "active-copy.o")], check=True)
    sizes = dict(zip(("edf_owner", "worker_transport", "main_transport", "aux_transport"),
                     struct.unpack("<4I", (out / "sizes.bin").read_bytes())))
    stack_rows = [line.rsplit("\t", 2) for line in (out / "active.su").read_text().splitlines()]
    maximum_stack_frame = max(int(row[1]) for row in stack_rows)
    dependencies = {}
    text = (out / "active.d").read_text().replace("\\\n", " ")
    for name in shlex.split(text.split(":", 1)[1]):
        path = Path(name)
        if not path.is_absolute():
            path = build / path
        path = path.resolve()
        assert path.is_file()
        dependencies[str(path)] = digest(path)
    assert all(digest(root / name) == value for name, value in hashes.items())
    receipt = dict(passed=True, scope="unlinked_active_interfaces_actual_board_flags", firmware_linked=False,
                   hardware_operated=False, compiler_sha256=digest(Path(command[0])),
                   flags_source_sha256=digest(build / "compile_commands.json"), source_hashes=hashes,
                   dependencies=dependencies, object_sizes=sizes, maximum_static_stack_frame=maximum_stack_frame, object_sha256=digest(out / "active.o"))
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(dict(passed=True, dependencies=len(dependencies), object_sizes=sizes, maximum_static_stack_frame=maximum_stack_frame, object_sha256=receipt["object_sha256"])), flush=True)


if __name__ == "__main__":
    main()
