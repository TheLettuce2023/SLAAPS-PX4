#!/usr/bin/env bash
set -euo pipefail
cd "$PX4_DIAGNOSTIC_SOURCE"
python3 /output/arm_stage.py guard
arm-none-eabi-gcc --version
make px4_fmu-v6x_default -j8
test -s build/px4_fmu-v6x_default/px4_fmu-v6x_default.px4
python3 /output/arm_stage.py guard
