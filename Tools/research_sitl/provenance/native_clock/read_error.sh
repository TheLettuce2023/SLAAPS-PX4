#!/bin/sh
set -eu
. px4-alias.sh
param select fs/parameters.bson
research_provenance load imu.txt "${P2B_RUN_ID:?}"
ekf2 start
if research_provenance feed; then
    echo P3C_UNEXPECTED_FEED_ACCEPTANCE
    exit 1
fi
ekf2 stop
if research_provenance export observations.json; then
    echo P3C_UNEXPECTED_READ_ACCEPTANCE
    exit 1
fi
test -s merged.json
test -s clock.json
echo P3C_EXPECTED_READ_REJECTION
