#!/bin/bash

#
# The thermal driver will monitor the SoC temperature in a certain frequency.
# This is a basic test which only reads the current temperature.
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

soc=$(cat /sys/devices/soc0/soc_id)
case $soc in
i.MX7ULP)
    echo "Platform '$soc' does not have tempmon"
    exit $BAT_EXITCODE_SKIP
    ;;
esac

# get current temperature
temp=$(bat_read_temp)
echo "Current temp $temp"

if [[ $temp -gt 200000 || $temp -lt -100000 ]]; then
    echo "Failed temperature reading sanity check"
    exit 1
fi
