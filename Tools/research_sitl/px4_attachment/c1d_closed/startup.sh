#!/bin/sh
# Closed, disarmed diagnostic only. Never source rcS or launch output modules.
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set COM_ARM_ON_BOOT 0
param set COM_ARMABLE 0
research_native_closed bootstrap "${C1D_SOURCE_ID:?}" "${C1D_CONTROLLER_ID:?}" "${C1D_OWNER_ID:?}" "${C1D_RUN_ID:?}" "${C1D_PRODUCER_ID:?}" "${C1D_CLOCK_EPOCH:?}"
commander start
research_native_closed wait "${C1D_WINDOW_US:?}"
# This command itself obtains Commander stop's actual return, then verifies
# normal source completion and retirement BEFORE reading or opening a sink.
research_native_closed stop_export trace.json
if research_native_closed export absent-parent/trace.json; then exit 1; fi
if research_native_closed export /dev/full; then exit 1; fi
if research_native_closed export blocked.fifo; then exit 1; fi
research_native_closed export trace-repeat.json
echo C1D_SINK_FAILURES_REJECTED
echo C1D_TOPICS_BEGIN
uorb status
echo C1D_TOPICS_END
echo C1D_STARTUP_COMPLETE
