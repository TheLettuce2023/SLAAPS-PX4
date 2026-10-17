#!/bin/sh
# Capture-only startup. Never source stock rcS or start a control/output module.
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set COM_ARM_ON_BOOT 0
param set COM_ARMABLE 0
research_state_capture bootstrap "${C1C_PRODUCER_ID:?}" "${C1C_OBSERVER_ID:?}" "${C1C_CLOCK_EPOCH:?}"
commander start
capture_status=0
research_state_capture collect "${C1C_CAPTURE_WINDOW_US:?}" || capture_status=$?
# This always precedes evidence-file I/O, including collector failure paths.
commander stop
finish_status=0
research_state_capture finish || finish_status=$?
echo C1C_TOPICS_BEGIN
uorb status
echo C1C_TOPICS_END
test "$capture_status" -eq 0
test "$finish_status" -eq 0
echo C1C_STARTUP_COMPLETE
