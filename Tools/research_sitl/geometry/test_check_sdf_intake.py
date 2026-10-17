"""Synthetic non-operational tests for the read-only SDF intake audit."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from check_sdf_intake import audit


def sdf(link, extra=""):
    return f'<sdf version="1.9"><model name="vehicle">{link}{extra}</model></sdf>'


def link(name="body", mass="1", inertia="1 1 1 0 0 0", mesh=""):
    ixx, iyy, izz, ixy, ixz, iyz = inertia.split()
    visual = f"<visual><geometry><mesh><uri>{mesh}</uri></mesh></geometry></visual>" if mesh else ""
    return (f'<link name="{name}"><inertial><mass>{mass}</mass><inertia><ixx>{ixx}</ixx>'
            f'<iyy>{iyy}</iyy><izz>{izz}</izz><ixy>{ixy}</ixy><ixz>{ixz}</ixz><iyz>{iyz}</iyz>'
            f'</inertia></inertial>{visual}</link>')


class IntakeTest(unittest.TestCase):
    def setUp(self):
        self._temporary_directories = []

    def tearDown(self):
        for directory in self._temporary_directories:
            directory.cleanup()

    def run_audit(self, text, mesh=False):
        temporary = tempfile.TemporaryDirectory()
        self._temporary_directories.append(temporary)
        directory = Path(temporary.name)
        path = directory / "model.sdf"
        path.write_text(text, encoding="utf-8")
        if mesh:
            (directory / "meshes").mkdir()
            (directory / "meshes" / "body.dae").write_bytes(b"x")
        before = path.read_bytes()
        result = audit(path)
        self.assertEqual(before, path.read_bytes())
        json.dumps(result, allow_nan=False)
        return result

    def test_valid_diagonal_and_local_mesh(self):
        result = self.run_audit(sdf(link(mesh="model://vehicle/meshes/body.dae")), True)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["readiness"], "structural_checks_only")
        self.assertEqual(result["links"], [{"name": "body", "mass": 1.0}])
        self.assertTrue(result["mesh_references"][0]["exists"])
        self.assertNotEqual(result["readiness"], "flightvalidated")

    def test_inertia_failures(self):
        triangle = self.run_audit(sdf(link(inertia="3 1 1 0 0 0")))
        self.assertTrue(any("triangle" in error for error in triangle["errors"]))
        offdiag = self.run_audit(sdf(link(inertia="2 2 2 1.5 0 0")))
        self.assertTrue(any("triangle" in error for error in offdiag["errors"]))
        bad = self.run_audit(sdf(link(mass="-1", inertia="nan 1 1 0 0 0")))
        self.assertTrue(any("mass" in error for error in bad["errors"]))
        self.assertTrue(any("nonfinite" in error for error in bad["errors"]))

    def test_names_xml_and_mesh_safety(self):
        duplicate = self.run_audit(sdf(link() + link()))
        self.assertTrue(any("Duplicate" in error for error in duplicate["errors"]))
        malformed = self.run_audit("<sdf><model>")
        self.assertTrue(any("parse" in error for error in malformed["errors"]))
        for uri in ("model://vehicle/missing.dae", "model://vehicle/../outside.dae",
                    "file:///outside.dae", "model://other/meshes/a.dae"):
            result = self.run_audit(sdf(link(mesh=uri)))
            self.assertTrue(result["errors"], uri)

    def test_joint_references_axis_and_limits(self):
        joint = '<joint name="j" type="revolute"><parent>body</parent><child>body</child><axis><xyz>0 0 0</xyz><limit><lower>2</lower><upper>1</upper></limit></axis></joint>'
        result = self.run_audit(sdf(link())[:-14] + joint + "</model></sdf>")
        self.assertTrue(any("parent/child" in error for error in result["errors"]))
        self.assertTrue(any("axis" in error for error in result["errors"]))
        self.assertTrue(any("reversed" in error for error in result["errors"]))

    def test_missing_mass_inertial_and_positive_definite(self):
        missing = self.run_audit('<sdf><model name="vehicle"><link name="body"/></model></sdf>')
        self.assertTrue(any("mass" in error for error in missing["errors"]))
        self.assertTrue(any("inertial" in error for error in missing["errors"]))
        zero = self.run_audit(sdf(link(inertia="0 0 0 0 0 0")))
        self.assertTrue(any("positive definite" in error for error in zero["errors"]))

    def test_model_shape_and_missing_source(self):
        for text in ('<sdf><world name="w"/></sdf>',
                     '<sdf><model name="vehicle"><model name="nested"/></model></sdf>'):
            self.assertTrue(self.run_audit(text)["errors"])
        missing = audit(Path(tempfile.gettempdir()) / "no-such-intake.sdf")
        self.assertEqual(missing["readiness"], "blocked")
        self.assertTrue(missing["errors"])

    def test_uri_platform_forms_and_symlink_escape(self):
        for uri in ("model://vehicle/C:/x.dae", "model://vehicle/\\x.dae",
                    "model://vehicle//x.dae"):
            self.assertTrue(self.run_audit(sdf(link(mesh=uri)))["errors"])
        temporary = tempfile.TemporaryDirectory()
        self._temporary_directories.append(temporary)
        directory = Path(temporary.name)
        package = directory / "package"
        package.mkdir()
        outside = directory / "outside.dae"
        outside.write_bytes(b"x")
        (package / "escape.dae").symlink_to(outside)
        path = package / "model.sdf"
        path.write_text(sdf(link(mesh="model://vehicle/escape.dae")), encoding="utf-8")
        self.assertTrue(audit(path)["errors"])

    def test_valid_joint_and_total_mass_overflow(self):
        valid = sdf(link("a") + link("b") + '<joint name="j" type="revolute"><parent>a</parent><child>b</child><axis><xyz>0 0 1</xyz><limit><lower>-1</lower><upper>1</upper></limit></axis></joint>')
        self.assertEqual(self.run_audit(valid)["errors"], [])
        overflow = self.run_audit(sdf(link("a", "1e308") + link("b", "1e308")))
        self.assertTrue(any("Total mass" in error for error in overflow["errors"]))


if __name__ == "__main__":
    unittest.main()
