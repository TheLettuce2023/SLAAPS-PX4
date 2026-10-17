"""Read-only SDF intake checks, not Gazebo schema or flight-model validation."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def audit(path: Path) -> dict:
    path = Path(path).resolve()
    result = {
        "schema_version": 1, "source": str(path), "model": None,
        "errors": [], "warnings": [
            "Structural checks only: geometry, units, aerodynamic/actuator coefficients "
            "and sensor behavior require provenance and independent validation."
        ], "links": [], "total_declared_mass": 0.0, "mesh_references": [],
        "plugins": [], "sensors": [], "readiness": "blocked",
    }
    errors = result["errors"]
    try:
        data = path.read_bytes()
        result["sha256"] = hashlib.sha256(data).hexdigest()
        root = ET.fromstring(data)
    except (OSError, ET.ParseError) as exc:
        errors.append(f"Cannot read/parse SDF: {exc}")
        return result
    models = root.findall("model")
    if root.tag != "sdf" or len(models) != 1:
        errors.append("Expected one direct model inside sdf; worlds/includes are outside this intake checker.")
        return result
    model = models[0]
    name = model.get("name", "")
    result["model"] = name
    if not name:
        errors.append("Model name missing")
    if model.findall("model") or model.findall("include"):
        errors.append("Nested models/includes are not resolved by this intake checker")

    def number(element, key, label):
        try:
            value = float(element.findtext(key, ""))
            if not math.isfinite(value):
                raise ValueError("nonfinite")
            return value
        except (ValueError, TypeError):
            errors.append(f"{label}: missing/nonfinite/invalid {key}")
            return None

    def det(a, b, c, d, e, f):
        return a * b * c + 2 * d * e * f - a * f * f - b * e * e - c * d * d

    names = set()
    for link in model.findall("link"):
        label = link.get("name", "")
        if not label or label in names:
            errors.append(f"Duplicate/missing link name: {label}")
        names.add(label)
        inertial = link.find("inertial")
        mass = number(link, "inertial/mass", label)
        result["links"].append({"name": label, "mass": mass})
        if mass is not None:
            if mass <= 0:
                errors.append(f"{label}: mass must be positive")
            else:
                result["total_declared_mass"] += mass
        if inertial is None:
            errors.append(f"{label}: explicit inertial required for this intake profile")
            continue
        values = [number(inertial, "inertia/" + key, label)
                  for key in ("ixx", "iyy", "izz", "ixy", "ixz", "iyz")]
        if any(value is None for value in values):
            continue
        # Normalize first to avoid overflow/underflow in determinant checks.
        scale = max(abs(value) for value in values)
        if scale == 0:
            errors.append(f"{label}: inertia must be positive definite")
            continue
        a, b, c, d, e, f = [value / scale for value in values]
        if a <= 0 or a * b - d * d <= 0 or det(a, b, c, d, e, f) <= 0:
            errors.append(f"{label}: inertia is not positive definite")
        # Q = trace(I)/2 * identity - I must be positive semidefinite for
        # a realizable mass distribution. This includes triangle inequalities.
        qx, qy, qz = (b + c - a) / 2, (a + c - b) / 2, (a + b - c) / 2
        minors = [qx, qy, qz, qx * qy - d * d, qx * qz - e * e,
                  qy * qz - f * f, det(qx, qy, qz, -d, -e, -f)]
        if min(minors) < -1e-12:  # relative floating arithmetic tolerance, not physical uncertainty
            errors.append(f"{label}: inertia violates physical triangle/second-moment constraints")
    if not names:
        errors.append("No direct links found")
    if not math.isfinite(result["total_declared_mass"]):
        result["total_declared_mass"] = None
        errors.append("Total mass arithmetic overflow")

    joint_names = set()
    for joint in model.findall("joint"):
        label = joint.get("name", "")
        if not label or label in joint_names:
            errors.append(f"Duplicate/missing joint name: {label}")
        joint_names.add(label)
        parent, child = joint.findtext("parent"), joint.findtext("child")
        if (parent not in names and parent != "world") or child not in names or parent == child:
            errors.append(f"{label}: invalid parent/child link reference")
        if joint.get("type") == "revolute":
            try:
                axis = [float(value) for value in joint.findtext("axis/xyz", "").split()]
                if len(axis) != 3 or not all(math.isfinite(value) for value in axis) or not any(axis):
                    raise ValueError("invalid axis")
            except ValueError:
                errors.append(f"{label}: revolute axis must be finite and nonzero")
            lower = number(joint, "axis/limit/lower", label)
            upper = number(joint, "axis/limit/upper", label)
            if lower is not None and upper is not None and lower > upper:
                errors.append(f"{label}: reversed joint limits")

    for mesh in model.findall(".//mesh"):
        uri = (mesh.findtext("uri") or "").strip()
        prefix = f"model://{name}/"
        entry = {"uri": uri, "resolved": None, "exists": False}
        result["mesh_references"].append(entry)
        if not uri.startswith(prefix):
            errors.append(f"Unsupported/nonlocal mesh URI: {uri}")
            continue
        suffix = uri[len(prefix):]
        relative = Path(suffix)
        # Reject platform-independent absolute paths, traversal and drive syntax.
        if not suffix or suffix.startswith("/") or ":" in suffix or chr(92) in suffix or ".." in relative.parts:
            errors.append(f"Unsafe mesh URI: {uri}")
            continue
        resolved = (path.parent / relative).resolve()
        if not resolved.is_relative_to(path.parent):
            errors.append(f"Mesh path escapes package: {uri}")
            continue
        entry["resolved"] = str(resolved)
        entry["exists"] = resolved.is_file()
        if not entry["exists"]:
            errors.append(f"Missing packaged mesh: {uri}")

    result["plugins"] = [dict(element.attrib) for element in model.findall(".//plugin")]
    result["sensors"] = [dict(element.attrib) for element in model.findall(".//sensor")]
    if not (path.parent / "model.config").is_file():
        result["warnings"].append("No sibling model.config; model discovery/package setup remains unverified.")
    if not errors:
        result["readiness"] = "structural_checks_only"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdf", type=Path)
    args = parser.parse_args()
    result = audit(args.sdf)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
