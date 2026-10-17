#!/bin/sh
# Output-disabled EKF observation. No rcS, Commander, control or network endpoint.
set -eu
. px4-alias.sh
param select fs/parameters.bson
research_provenance load imu.txt "${P2B_RUN_ID:?}"
ekf2 start
research_provenance feed
ekf2 stop
research_provenance export observations.json
echo P2B_TOPICS_BEGIN
uorb status
echo P2B_TOPICS_END
echo P2B_CAPTURE_COMPLETE
