#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

set -e

soc=$(cat /sys/devices/soc0/soc_id)
case $soc in
i.MX7ULP)
    type=standby
    ;;
*)
    type=mem
    ;;
esac

old_wakeup_count=$(cat /sys/power/wakeup_count)
$batdir/../SRTC/rtcwakeup.out -d rtc0 -m $type -s 5
new_wakeup_count=$(cat /sys/power/wakeup_count)

delta=$[$new_wakeup_count-$old_wakeup_count]
echo "Suspend count delta: $delta"
if [ $delta -lt 1 ]; then
    exit 1
fi
