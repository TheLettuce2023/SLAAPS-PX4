#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set SENS_IMU_MODE 0
param set EKF2_MULTI_IMU 2
research_selector_handoff begin "${P2J_RUN_ID:?}"
research_multi_imu load imu.txt system_state.txt "${P2J_RUN_ID:?}"
research_multi_imu sensors "${P2J_ACCEL_DEVICE_ID:?}" "${P2J_GYRO_DEVICE_ID:?}"
research_multi_imu prime
ekf2 start
research_multi_imu feed_until_pair
ekf2 stop 0
research_multi_imu feed_after_stop
research_selector_handoff prepare
research_multi_imu prepare
ekf2 stop
ekf2 start
ekf2 stop
research_multi_imu finish feeder.json system_state.json
if research_selector_handoff finish handoff.json; then
    echo P2J_UNEXPECTED_RESTART_ACCEPTANCE
    exit 1
fi
test -s handoff.json
echo P2J_EXPECTED_RESTART_REJECTION
