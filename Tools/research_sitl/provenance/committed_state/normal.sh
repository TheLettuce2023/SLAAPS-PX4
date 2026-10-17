#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
research_provenance load imu.txt "${P2B_RUN_ID:?}"
research_provenance sensors "${P2E_ACCEL_DEVICE_ID:?}" "${P2E_GYRO_DEVICE_ID:?}"
ekf2 start
research_provenance feed
ekf2 stop
research_provenance export observations.json
echo P2E_CAPTURE_COMPLETE
