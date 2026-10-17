#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set SENS_IMU_MODE 0
param set EKF2_MULTI_IMU 1
research_selector_disposition begin "${P2H_RUN_ID:?}"
research_selector_provenance load imu.txt "${P2H_RUN_ID:?}"
research_selector_provenance sensors "${P2H_ACCEL_DEVICE_ID:?}" "${P2H_GYRO_DEVICE_ID:?}"
research_selector_provenance prime
ekf2 start
research_selector_provenance feed
research_selector_disposition prepare
research_selector_provenance prepare
ekf2 stop
ekf2 start
ekf2 stop
if research_selector_provenance finish selector.json feeder.json; then
    echo P2H_UNEXPECTED_P2G_RESTART_ACCEPTANCE
    exit 1
fi
if research_selector_disposition finish disposition.json; then
    echo P2H_UNEXPECTED_DISPOSITION_RESTART_ACCEPTANCE
    exit 1
fi
test -s selector.json
test -s feeder.json
test -s disposition.json
echo P2H_EXPECTED_RESTART_REJECTION
