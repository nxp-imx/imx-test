#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

if ! bat_has_busfreq; then
    echo "Missing busfreq node in device-tree, seems not implemented"
    exit "$BAT_EXITCODE_SKIP"
fi

# clear dmesg, we don't care about old messages
dmesg -C

bat_lowbus_prepare

BUSFREQ_SLEEP_TIME=10

echo "Sleep $BUSFREQ_SLEEP_TIME seconds waiting for busfreq" >&2
$batdir/bat_netpause.sh $BUSFREQ_SLEEP_TIME
echo "Sleep waiting for busfreq over" >&2

busfreq=$(dmesg | grep "\(Bus freq\|ddrc freq\|Busfreq OPTEE\) set to" || true)
busfreq_lines=$(echo "$busfreq" | wc -l)

bat_lowbus_cleanup

echo -e "Busfreq messages:\n$busfreq"

if [ $busfreq_lines -lt 2 ]; then
    echo "Busfreq did NOT activate"
    exit 1;
else
    echo "Busfreq seems fine"
    exit 0;
fi
