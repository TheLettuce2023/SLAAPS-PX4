#!/usr/bin/env python3
"""Disposable synthetic config/hash negatives; not evidence of a real PX4 build."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("capture_runner_for_tests", HERE.parent / "run_capture.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ConfigurationBinding(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="c1c_binding_synthetic_")
        self.addCleanup(temporary.cleanup)
        self.area = Path(temporary.name)
        self.output = self.area / "build"
        self.checkout = self.area / "synthetic-checkout"
        self.capture_source = self.checkout / "capture"
        self.capture_source.mkdir(parents=True)
        self.write(self.capture_source / "commander_receipts.patch", b"synthetic patch bytes\n")
        self.board = "CONFIG_MODULES_COMMANDER=y\nCONFIG_BOARD_NOLOCKSTEP=y\n"
        self.write(self.output / "boardconfig", self.board.encode())
        self.apps = "\n".join('apps["' + name + '"] = nullptr;' for name in sorted(runner.BUILTINS))
        self.write(self.output / "platforms/posix/apps.cpp", self.apps.encode())
        self.write(self.output / "bin/px4", b"not an executable: synthetic artifact hash fixture\n")
        sources = [self.area / "source/src/modules/commander/Commander.cpp",
                   self.capture_source / "producer/StateCaptureProducer.cpp",
                   self.capture_source / "producer/Px4StateReceiptProducer.cpp",
                   self.capture_source / "px4_external/src/research_state_capture/research_state_capture.cpp"]
        self.commands = [{"file": path.as_posix(), "directory": self.output.as_posix(),
                          "command": "c++ -DRESEARCH_C1C_CAPTURE=1 -c " + shlex.quote(path.as_posix())}
                         for path in sources]
        self.save_commands(self.commands)
        self.pins = {}
        overlay = {}
        for relative in ("src/modules/commander/Commander.cpp", "src/modules/commander/Commander.hpp",
                         "src/modules/commander/CMakeLists.txt"):
            base = self.checkout / relative
            derived = self.area / "source" / relative
            self.write(base, ("synthetic base " + relative + "\r\n").encode())
            self.write(derived, ("synthetic derived " + relative + "\n").encode())
            self.pins[relative] = digest(base)
            normalized = hashlib.sha256(base.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
            overlay[relative] = {"base_sha256": digest(base), "normalized_sha256": normalized,
                                 "derived_sha256": digest(derived)}
        self.registry = {relative: digest(self.checkout / relative) for relative in self.pins}
        self.write(self.area / "source_hashes.json", json.dumps(self.registry).encode())
        for target, name, value in ((runner, "ROOT", self.checkout), (runner, "HERE", self.capture_source),
                                    (runner, "BASE_HASHES", self.pins),
                                    (runner.base, "source_hashes", lambda: copy.deepcopy(self.registry))):
            mocked = patch.object(target, name, value)
            mocked.start()
            self.addCleanup(mocked.stop)
        self.manifest = {"source_registry_sha256": digest(self.area / "source_hashes.json"),
                         "executable_sha256": digest(self.output / "bin/px4"),
                         "configuration_audit": runner.check_config(self.output),
                         "overlay": {"patch_sha256": digest(self.capture_source / "commander_receipts.patch"),
                                     "files": overlay}}

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def save_commands(self, commands):
        self.write(self.output / "compile_commands.json", json.dumps(commands).encode())

    def test_synthetic_baseline_only(self):
        runner.check_bound_inputs(self.area, self.manifest)
        self.assertEqual(runner.check_config(self.output)["enabled_modules"], ["CONFIG_MODULES_COMMANDER"])

    def test_configure_uses_owned_source_view_for_external_inputs(self):
        view = self.area / "source"
        relative = Path("Tools/research_sitl/px4_attachment/c1c_capture")
        raw_checkout = self.area / "raw checkout's directory"
        manifest = {}
        with patch.object(runner, "ROOT", raw_checkout), \
                patch.object(runner, "HERE", raw_checkout / relative), \
                patch.object(runner, "overlay_view", return_value=(view, {})), \
                patch.object(runner, "check_config", return_value={}), \
                patch.object(runner.base, "save"), \
                patch.object(runner.base, "digest", return_value="0" * 64), \
                patch.object(runner.base, "bounded_command", return_value=0) as execute:
            self.assertEqual(runner.build(self.area, manifest), self.output)
        self.assertEqual(execute.call_count, 2)
        configure = execute.call_args_list[0].args[0]
        definitions = dict(argument[2:].split("=", 1) for argument in configure if argument.startswith("-D"))
        self.assertEqual(definitions["PX4_CONFIG_FILE"], str(view / relative / "c1c-default.px4board"))
        self.assertEqual(definitions["EXTERNAL_MODULES_LOCATION"], str(view / relative / "px4_external"))
        self.assertTrue(all(str(raw_checkout) not in argument for argument in configure))
        self.assertEqual(execute.call_args_list[0].args[1], view)
        self.assertEqual(manifest["build"]["configure_command"], configure)

    def test_capture_command_uses_regular_staged_reviewed_startup(self):
        raw_source = self.area / "raw checkout's startup source"
        expected = b"#!/bin/sh\n# Synthetic bytes: this script must never execute.\n"
        self.write(raw_source / "startup.sh", expected)
        with patch.object(runner, "HERE", raw_source), \
                patch.object(runner.secrets, "randbelow", return_value=1), \
                patch.object(runner.secrets, "randbits", side_effect=[11, 13, 17]), \
                patch.object(runner.base, "save"), \
                patch.object(runner.subprocess, "Popen") as launch:
            launch.return_value.pid = 10003
            launch.return_value.returncode = 0
            launch.return_value.communicate.return_value = (None, None)
            record = runner.capture_run(self.area, self.output, 1, set(), set())
        runtime = self.area / "run-1"
        startup = runtime / "startup.sh"
        self.assertTrue(startup.is_file())
        self.assertFalse(startup.is_symlink())
        self.assertEqual(startup.read_bytes(), expected)
        self.assertEqual(record["startup_sha256"], hashlib.sha256(expected).hexdigest())
        launch.assert_called_once()
        self.assertEqual(launch.call_args.args[0][-2:], ["-s", str(startup)])
        self.assertEqual(launch.call_args.kwargs["cwd"], runtime)
        self.assertEqual(record["command"], launch.call_args.args[0])
        self.assertNotIn(str(raw_source), " ".join(record["command"]))
        launch.return_value.communicate.assert_called_once_with("shutdown\n", timeout=20)

    def test_unapproved_modules_drivers_and_clock_config(self):
        for extra in ("CONFIG_MODULES_MC_ATT_CONTROL=y", "CONFIG_MODULES_MAVLINK=y",
                      "CONFIG_DRIVERS_PWM_OUT=y", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP=y"):
            with self.subTest(setting=extra):
                self.write(self.output / "boardconfig", (self.board + extra + "\n").encode())
                with self.assertRaises(RuntimeError):
                    runner.check_config(self.output)

    def test_unapproved_builtin(self):
        self.write(self.output / "platforms/posix/apps.cpp", (self.apps + '\napps["actuator_test"] = nullptr;').encode())
        with self.assertRaises(RuntimeError):
            runner.check_config(self.output)

    def test_missing_builtin(self):
        self.write(self.output / "platforms/posix/apps.cpp", self.apps.replace('apps["commander"]', 'removed').encode())
        with self.assertRaises(RuntimeError):
            runner.check_config(self.output)

    def test_forbidden_clock_and_spoofed_host_flags(self):
        flags = ("ENABLE_LOCKSTEP_SCHEDULER", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP", "BOARD_LOCKSTEP",
                 "PX4_SIMULATOR_TIMESTAMP_PROXY", "RESEARCH_SITL_STATE_RECEIPT_HOST_TEST",
                 "RESEARCH_SITL_STATE_CAPTURE_HOST_TEST", "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST")
        for flag in flags:
            with self.subTest(flag=flag):
                commands = copy.deepcopy(self.commands)
                commands[0]["command"] += " -D" + flag + "=1"
                self.save_commands(commands)
                with self.assertRaises(RuntimeError):
                    runner.check_config(self.output)

    def test_capture_optin_is_exact(self):
        for replacement in ("", "-DRESEARCH_C1C_CAPTURE=0", "-DRESEARCH_C1C_CAPTURE",
                            "-DRESEARCH_C1C_CAPTURE=1 -DRESEARCH_C1C_CAPTURE=1"):
            with self.subTest(marker=replacement):
                commands = copy.deepcopy(self.commands)
                commands[0]["command"] = commands[0]["command"].replace("-DRESEARCH_C1C_CAPTURE=1", replacement)
                self.save_commands(commands)
                with self.assertRaises(RuntimeError):
                    runner.check_config(self.output)

    def test_all_capture_compilation_units_required_exactly_once(self):
        for index in range(4):
            for duplicate in (False, True):
                with self.subTest(unit=index, duplicate=duplicate):
                    commands = copy.deepcopy(self.commands)
                    if duplicate:
                        commands.append(copy.deepcopy(commands[index]))
                    else:
                        commands.pop(index)
                    self.save_commands(commands)
                    with self.assertRaises(RuntimeError):
                        runner.check_config(self.output)

    def test_marker_cannot_escape_isolated_source_scope(self):
        commands = copy.deepcopy(self.commands)
        commands.append({"file": (self.area / "unapproved/controller.cpp").as_posix(),
                         "command": "c++ -DRESEARCH_C1C_CAPTURE=1 -c controller.cpp"})
        self.save_commands(commands)
        with self.assertRaises(RuntimeError):
            runner.check_config(self.output)

    def test_external_source_symlink_requires_exact_original_identity(self):
        original = self.capture_source / "producer/StateCaptureProducer.cpp"
        self.write(original, b"synthetic original producer source\n")
        lexical = self.area / "source/capture/producer/StateCaptureProducer.cpp"
        lexical.parent.mkdir(parents=True, exist_ok=True)
        lexical.symlink_to(original)
        commands = copy.deepcopy(self.commands)
        commands[1]["file"] = lexical.as_posix()
        commands[1]["command"] = "c++ -DRESEARCH_C1C_CAPTURE=1 -c " + shlex.quote(lexical.as_posix())
        self.save_commands(commands)
        self.assertEqual(lexical.resolve(), original.resolve())
        runner.check_config(self.output)
        foreign = self.area / "foreign/StateCaptureProducer.cpp"
        self.write(foreign, b"synthetic unrelated producer source\n")
        lexical.unlink()
        lexical.symlink_to(foreign)
        with self.assertRaises(RuntimeError):
            runner.check_config(self.output)

    def test_each_artifact_hash_binding(self):
        paths = [self.area / "source_hashes.json", self.output / "bin/px4", self.output / "boardconfig",
                 self.capture_source / "commander_receipts.patch",
                 self.checkout / "src/modules/commander/Commander.cpp",
                 self.area / "source/src/modules/commander/Commander.cpp"]
        for path in paths:
            with self.subTest(path=path.relative_to(self.area)):
                original = path.read_bytes()
                path.write_bytes(original + b"changed\n")
                with self.assertRaises(RuntimeError):
                    runner.check_bound_inputs(self.area, self.manifest)
                path.write_bytes(original)

    def test_rehashed_registry_cannot_hide_stale_source(self):
        altered = copy.deepcopy(self.registry)
        altered["src/modules/commander/Commander.cpp"] = "0" * 64
        self.write(self.area / "source_hashes.json", json.dumps(altered).encode())
        self.manifest["source_registry_sha256"] = digest(self.area / "source_hashes.json")
        with self.assertRaises(RuntimeError):
            runner.check_bound_inputs(self.area, self.manifest)

    def test_overlay_registry_cannot_omit_target(self):
        self.manifest["overlay"]["files"].pop("src/modules/commander/Commander.hpp")
        with self.assertRaises(RuntimeError):
            runner.check_bound_inputs(self.area, self.manifest)

    def test_false_normalized_hash_even_with_unchanged_source(self):
        hashes = self.manifest["overlay"]["files"]["src/modules/commander/Commander.cpp"]
        self.assertNotEqual(hashes["normalized_sha256"], hashes["base_sha256"])
        hashes["normalized_sha256"] = hashes["base_sha256"]
        with self.assertRaisesRegex(RuntimeError, "normalized"):
            runner.check_bound_inputs(self.area, self.manifest)


if __name__ == "__main__":
    unittest.main()
