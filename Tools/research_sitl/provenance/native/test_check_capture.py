import copy
import tempfile
from pathlib import Path
import unittest

from check_capture import observations, sha, source_binding


class NativeEvidenceTest(unittest.TestCase):
    def test_source_inventory_and_executed_startup(self):
        with tempfile.TemporaryDirectory() as directory:
            area = Path(directory).resolve()
            archive = area / "source-inputs-test"
            runtime = area / "run-1"; runtime.mkdir()
            prefix = "Tools/research_sitl/provenance/native/"
            registry, overlay = {}, {}
            for rel in ("src/modules/ekf2/EKF2.cpp", "src/modules/ekf2/CMakeLists.txt"):
                path = area / "source" / rel
                path.parent.mkdir(parents=True, exist_ok=True); path.write_text(rel)
                registry[rel] = sha(path)
                overlay[rel] = dict(original=sha(path), derived=sha(path))
            for rel in (prefix + "startup.sh", prefix + "run_capture.py"):
                path = archive / rel
                path.parent.mkdir(parents=True, exist_ok=True); path.write_text(rel)
                registry[rel] = sha(path)
            startup = runtime / "startup.sh"; startup.write_text(prefix + "startup.sh")
            manifest = dict(overlay=overlay, native_source_archive=str(archive))
            source_binding(area, manifest, registry, runtime)
            for corruption in ("missing_archive", "missing_file", "extra_file", "empty_overlay", "startup"):
                sample = copy.deepcopy(manifest)
                missing = archive / (prefix + "run_capture.py")
                extra = archive / "extra"
                if corruption == "missing_archive": sample["native_source_archive"] = str(area / "source-inputs-missing")
                elif corruption == "missing_file": missing.unlink()
                elif corruption == "extra_file": extra.write_text("extra")
                elif corruption == "empty_overlay": sample["overlay"] = {}
                else: startup.write_text("modified")
                with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                    source_binding(area, sample, registry, runtime)
                missing.write_text(prefix + "run_capture.py")
                if extra.exists(): extra.unlink()
                startup.write_text(prefix + "startup.sh")

    def fixture(self):
        records = []
        for i, stamp in enumerate((10, 30)):
            records.append(dict(sequence=i+1, sample_us=stamp, observed_us=stamp+2,
                                control_flags=i, fault_flags=0, attitude_valid=bool(i),
                                horizontal_valid=False, vertical_position_valid=False, vertical_velocity_valid=False))
        return dict(schema="native-ekf-observation/v1", run_id="7", input_count=3,
                    fault=0, count=2, admission_instantiated=False, clock_map_qualified=False,
                    input_timestamps=[10, 20, 30], records=records)

    def test_loss_is_visible_not_qualified(self):
        result = observations(self.fixture(), "7")
        self.assertEqual(result["unobserved_input_publications"], 1)
        self.assertEqual(result["observed_state_changes"], 1)
        self.assertFalse(result["publisher_continuity_qualified"])

    def test_fault_authority_and_schema_corruption(self):
        for field, value in (("fault", 1), ("count", True), ("count", 2049), ("clock_map_qualified", True),
                             ("run_id", "8"), ("admission_instantiated", True), ("extra", 0)):
            sample = self.fixture(); sample[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): observations(sample, "7")

    def test_source_order_and_predicate_corruption(self):
        for field, value in (("sequence", 3), ("sample_us", 20), ("sample_us", 9), ("observed_us", 29),
                             ("attitude_valid", False), ("horizontal_valid", 1), ("control_flags", 1 << 64)):
            sample = self.fixture(); sample["records"][1][field] = value
            # A legitimate later input sample at 20 is accepted; make it foreign instead.
            if field == "sample_us" and value == 20: sample["records"][1][field] = 21
            with self.subTest(field=field, value=value), self.assertRaises(ValueError): observations(sample, "7")
        sample = copy.deepcopy(self.fixture()); sample["input_timestamps"] = [10, 10, 30]
        with self.assertRaises(ValueError): observations(sample, "7")


if __name__ == "__main__":
    unittest.main()
