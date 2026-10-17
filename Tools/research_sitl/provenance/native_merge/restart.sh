#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
research_provenance load imu.txt "${P2B_RUN_ID:?}"
ekf2 start
research_provenance feed
ekf2 stop
ekf2 start
ekf2 stop
if research_provenance export observations.json; then
    echo P2D_UNEXPECTED_RESTART_ACCEPTANCE
    exit 1
fi
test -s merged.json
echo P2D_EXPECTED_RESTART_REJECTION
