#!/usr/bin/env python3
"""Archive Git inputs and bind a stock SITL build to their exact bytes (WSL)."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tarfile

from acceptance import reference
from inspect_raw_ulog import digest, require
from score_interval import REGISTRY_PATH, registry


def now():
    return datetime.now(timezone.utc).isoformat()


def write_new(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def git(repo, *args):
    executable, location = "git", str(repo)
    native_git = Path("/mnt/c/Program Files/Git/cmd/git.exe")
    if location.startswith("/mnt/c/") and native_git.is_file():
        executable = str(native_git)
        location = "C:/" + location[len("/mnt/c/"):]
    environment = os.environ.copy()
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    return subprocess.check_output([executable, "-c", "safe.directory=" + location, "-C", location, *args],
                                   env=environment)


def source_inventory(source):
    entries, repositories = [], []
    pinned = {"src/modules/simulation/sensor_airspeed_sim/SensorAirspeedSim.cpp",
              "ROMFS/px4fmu_common/init.d-posix/airframes/4003_gz_rc_cessna",
              "Tools/simulation/gz/models/rc_cessna/model.sdf",
              "Tools/simulation/gz/worlds/default.sdf"}

    def visit(repo, prefix):
        repositories.append({"path": prefix or ".", "revision": git(repo, "rev-parse", "HEAD").decode().strip(),
                             "status": git(repo, "status", "--porcelain=v1", "--untracked-files=all").decode(),
                             "diff_from_head": git(repo, "diff", "--binary", "HEAD").decode()})
        gitlinks = {}
        for raw in git(repo, "ls-files", "--stage", "-z").split(b"\0"):
            if raw:
                meta, name = raw.split(b"\t", 1)
                mode, revision, stage = meta.split()
                require(stage == b"0", "unmerged source index")
                if mode == b"160000":
                    gitlinks[os.fsdecode(name)] = revision.decode()
        names = set(os.fsdecode(p) for p in git(repo, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0") if p)
        names.update(os.fsdecode(p) for p in git(repo, "diff", "--name-only", "-z", "HEAD").split(b"\0") if p)
        names.update(gitlinks)
        for name in pinned:
            if prefix and name.startswith(prefix + "/"):
                names.add(name[len(prefix) + 1:])
            elif not prefix and not name.startswith("Tools/simulation/gz/"):
                names.add(name)
        for name in sorted(names):
            relative = str(Path(prefix) / name)
            path = repo / name
            if name in gitlinks:
                if (path / ".git").exists():
                    visit(path, relative)
                else:
                    repositories.append({"path": relative, "revision": gitlinks[name],
                                         "status": "uninitialized; no working-tree input"})
                continue
            try:
                info = path.lstat()
            except FileNotFoundError:
                # A tracked deletion is recorded in repository status.
                continue
            if stat.S_ISLNK(info.st_mode):
                entries.append({"path": relative, "symlink": os.readlink(path), "mode": info.st_mode & 0o7777})
            elif stat.S_ISREG(info.st_mode):
                entries.append({"path": relative, "sha256": digest(path), "bytes": info.st_size,
                                "mode": info.st_mode & 0o7777})
            else:
                raise ValueError("unexpected source path: " + str(path))
    visit(source, "")
    entries.sort(key=lambda item: item["path"])
    return {"schema": "sitl-source-snapshot/v1",
            "scope": "all dirty tracked and nonignored untracked files plus critical model/publisher inputs; clean sources identified by Git revisions",
            "repositories": repositories, "files": entries}


@contextmanager
def mounted_source(source, mount, build):
    require(os.geteuid() == 0, "mount workflow requires WSL root")
    # A WSL invocation can inherit a cwd resolved through the bind alias.
    os.chdir("/")
    mount.mkdir(parents=True, exist_ok=True)
    require(build.is_dir() and source.is_dir(), "source/build directory absent")
    targets = [mount, mount / "build"]
    for target in targets:
        require(subprocess.run(["mountpoint", "-q", str(target)]).returncode != 0,
                "refusing an existing mount: " + str(target))
    owned = []
    try:
        for original, target in ((source, mount), (build, mount / "build")):
            subprocess.run(["mount", "--bind", str(original), str(target)], check=True)
            owned.append(target)
        yield
    finally:
        for target in reversed(owned):
            subprocess.run(["umount", str(target)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "source", "mount", "build", "parameters"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--linux-user", default="kspry04")
    args = parser.parse_args()
    registry()
    args.root.mkdir(parents=True, exist_ok=False)
    root = args.root.resolve()
    source = args.source.resolve()
    print("Archiving source inputs", flush=True)
    before = source_inventory(source)
    write_new(root / "source-index.json", before)
    with tarfile.open(root / "source.tar.gz", "w:gz") as archive:
        for entry in before["files"]:
            archive.add(source / entry["path"], arcname=entry["path"], recursive=False)
    for original, name in ((REGISTRY_PATH, "registry.json"),
                           (source / "docs/SITL_BASELINE_DIAGNOSTIC_LIMITS_V2_APPROVAL.md", "approval.md"),
                           (source / "docs/SITL_BASELINE_REPRODUCIBILITY.md", "signal-inventory.md"),
                           (args.parameters, "parameters.bson")):
        shutil.copyfile(original, root / name)
    command = ["runuser", "-u", args.linux_user, "--", "make", "px4_sitl_default"]
    started = now()
    print("Building stock SITL against frozen source snapshot", flush=True)
    with mounted_source(source, args.mount, args.build):
        with (root / "build.log").open("xb") as output:
            process = subprocess.run(command, cwd=args.mount, stdout=output, stderr=subprocess.STDOUT)
    require(process.returncode == 0, "build failed; inspect build.log")
    after = source_inventory(source)
    require(before == after, "source changed during build; package cannot be accepted")
    shutil.copyfile(args.build / "px4_sitl_default/bin/px4", root / "px4")
    write_new(root / "build.json", {
        "schema": "stock-cessna-build/v1", "started_utc": started, "finished_utc": now(),
        "command": command, "source_mount": str(args.mount), "native_build": str(args.build),
        "exit_code": process.returncode, "source_index_sha256": digest(root / "source-index.json"),
        "source_verified_after_build": True, "executable": reference(root / "px4", root),
        "transcript": reference(root / "build.log", root)})
    print(json.dumps({"root": str(root), "source_files": len(before["files"]),
                      "repositories": len(before["repositories"]), "executable_sha256": digest(root / "px4")}), flush=True)


if __name__ == "__main__":
    main()
