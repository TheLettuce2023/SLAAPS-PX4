#!/bin/sh
# Signed negative-only diagnostic. No rcS, actuator, control or output module.
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set COM_ARM_ON_BOOT 0
param set COM_ARMABLE 0
research_native_ingress bootstrap "${C1E_SOURCE_ID:?}" "${C1E_CONTROLLER_ID:?}" "${C1E_OWNER_ID:?}" "${C1E_RUN_ID:?}" "${C1E_PRODUCER_ID:?}" "${C1E_CLOCK_ID:?}" "${C1E_TRANSPORT_ID:?}" "${C1E_LOCAL_PORT:?}" "${C1E_PEER_PORT:?}" "${C1E_KEY_FILE:?}"
commander start
research_native_ingress checkpoint
research_native_ingress wait "${C1E_WINDOW_US:?}"
# The command itself obtains Commander's actual stop result and joins the
# worker before any archive read or sink access.
research_native_ingress stop_export trace.json
research_native_ingress export trace-repeat.json
echo C1E_TOPICS_BEGIN
uorb status
echo C1E_TOPICS_END
echo C1E_STARTUP_COMPLETE
