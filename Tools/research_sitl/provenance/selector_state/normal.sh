#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set SENS_IMU_MODE 0
param set EKF2_MULTI_IMU 1
research_selector_provenance load imu.txt "${P2G_RUN_ID:?}"
research_selector_provenance sensors "${P2G_ACCEL_DEVICE_ID:?}" "${P2G_GYRO_DEVICE_ID:?}"
research_selector_provenance prime
ekf2 start
research_selector_provenance feed
research_selector_provenance prepare
ekf2 stop
research_selector_provenance finish selector.json feeder.json
echo P2G_CAPTURE_COMPLETE
