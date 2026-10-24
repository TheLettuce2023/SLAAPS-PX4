#!/usr/bin/env python3
"""Reuse physical MAIN/AUX PWM metadata in the capture-only native SLAAPS test.

SITL normally defines only simulated output functions. This test needs the real
calibration parameters consumed by MixingOutput, without linking/running a PWM
hardware driver. Parameter definitions remain owned by the physical drivers.
"""
import argparse
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    main_config = yaml.safe_load((ROOT / "src/drivers/px4io/module.yaml").read_text())
    aux_text = (ROOT / "src/drivers/pwm_out/module.yaml").read_text()
    aux_text = aux_text.replace("${PWM_MAIN_OR_AUX_CAP}", "Capture").replace("${PWM_MAIN_OR_AUX}", "PWM_AUX")
    aux_config = yaml.safe_load(aux_text)
    aux_group = aux_config["actuator_output"]["output_groups"][0]
    aux_group["timer_config_file"] = "boards/px4/fmu-v6x/src/timer_config.cpp"
    main_config["module_name"] = "SLAAPS native PWM metadata (no hardware)"
    main_config["actuator_output"]["output_groups"].append(aux_group)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(yaml.safe_dump(main_config, sort_keys=False))


if __name__ == "__main__":
    main()
