"""Measure ARM type layouts for the prior and current observer; no execution."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

OUT = Path("/output")
CURRENT = Path("/work/px4-stabilized-observer")
PRIOR = Path("/donor/px4-stabilized-observer")
TARGET = "px4_fmu-v6x_default"
MODULE = "src/modules/research_stabilized_manual/ResearchStabilizedManual.cpp"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    destination = OUT / "layout"
    destination.mkdir(exist_ok=False)
    layouts = {}
    for label, root in (("prior", PRIOR), ("current", CURRENT)):
        database = json.loads((root / "build" / TARGET / "compile_commands.json").read_text())
        entry, = [item for item in database if item["file"].endswith("/" + MODULE)]
        probe = destination / (label + ".cpp")
        probe.write_text(
            '#include "' + str(root / MODULE) + '"\n'
            "#include <uORB/uORBDeviceNode.hpp>\n"
            "unsigned char layout_node[sizeof(uORB::DeviceNode)];\n"
            "unsigned char layout_observer[sizeof(ResearchStabilizedManual)];\n"
            "unsigned char layout_diagnostic[sizeof(stabilized_observer_status_s)];\n")
        command = []
        skip = False
        for token in shlex.split(entry["command"]):
            if skip:
                skip = False
                continue
            if token in ("-o", "-MF", "-MT", "-MQ"):
                skip = True
                continue
            if token in ("-MD", "-MMD"):
                continue
            token = token.replace(str(CURRENT), str(root))
            command.append(str(probe) if token.endswith("/" + MODULE) else token)
        obj = destination / (label + ".o")
        dep = destination / (label + ".d")
        command += ["-o", str(obj), "-MD", "-MF", str(dep)]
        (destination / (label + "-command.json")).write_text(json.dumps(command, indent=2) + "\n")
        with (destination / (label + "-compile.log")).open("x") as log:
            subprocess.run(command, cwd=entry["directory"].replace(str(CURRENT), str(root)),
                           env=dict(os.environ, CCACHE_DISABLE="1"), stdout=log, stderr=subprocess.STDOUT, check=True)
        symbols = subprocess.check_output(["arm-none-eabi-nm", "-S", str(obj)], text=True)
        (destination / (label + "-symbols.txt")).write_text(symbols)
        values = {}
        for size, name in re.findall(r"^\S+\s+(\S+)\s+B\s+layout_(\w+)$", symbols, re.M):
            values[name] = int(size, 16)
        assert set(values) == {"node", "observer", "diagnostic"}
        layouts[label] = values
        for name in shlex.split(dep.read_text().replace("\\\n", "").split(":", 1)[1]):
            path = Path(name)
            if not path.is_absolute():
                path = Path(entry["directory"].replace(str(CURRENT), str(root))) / path
            path = path.resolve()
            target = destination / "inputs" / str(path).lstrip("/")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    result = {
        "scope": "ARM compiler sizeof via unlinked BSS arrays; not runtime allocation or stack measurements",
        "firmware_receipt_sha256": digest(OUT / "firmware-receipt.json"),
        "sizes": layouts,
        "deltas": {name: layouts["current"][name] - layouts["prior"][name] for name in layouts["prior"]},
        "hardware_operated": False,
        "files": {p.relative_to(destination).as_posix(): digest(p) for p in destination.rglob("*") if p.is_file()},
    }
    (destination / "layout-receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"sizes": layouts, "deltas": result["deltas"]}, indent=2))


if __name__ == "__main__":
    main()
