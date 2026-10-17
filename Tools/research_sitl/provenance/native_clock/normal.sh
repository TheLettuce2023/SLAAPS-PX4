#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
research_provenance load imu.txt "${P2B_RUN_ID:?}"
ekf2 start
research_provenance feed
ekf2 stop
research_provenance export observations.json
echo P3C_CAPTURE_COMPLETE
