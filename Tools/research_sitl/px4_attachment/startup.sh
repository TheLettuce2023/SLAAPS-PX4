#!/bin/sh
# C1A passive startup; never source stock rcS or start any output module.
set -eu
. px4-alias.sh
param select fs/parameters.bson
param set COM_ARM_ON_BOOT 0
param set COM_ARMABLE 0
commander start
research_probe
echo C1A_TOPICS_BEGIN
uorb status
echo C1A_TOPICS_END
commander stop
echo C1A_STARTUP_COMPLETE
