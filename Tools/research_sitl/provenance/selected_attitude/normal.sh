#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set SENS_IMU_MODE 0
param set EKF2_MULTI_IMU 1
research_selected_attitude begin "${P2I_RUN_ID:?}"
research_selector_disposition begin "${P2I_RUN_ID:?}"
research_selector_provenance load imu.txt system_state.txt "${P2I_RUN_ID:?}"
research_selector_provenance sensors "${P2I_ACCEL_DEVICE_ID:?}" "${P2I_GYRO_DEVICE_ID:?}"
research_selector_provenance prime
ekf2 start
research_selector_provenance feed
research_selected_attitude prepare
research_selector_disposition prepare
research_selector_provenance prepare
ekf2 stop
research_selector_provenance finish selector.json feeder.json system_state.json
research_selector_disposition finish disposition.json
research_selected_attitude finish selected_attitude.json
echo P2I_CAPTURE_COMPLETE
