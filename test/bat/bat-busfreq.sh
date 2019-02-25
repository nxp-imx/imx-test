#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

bat_has_busfreq()
{
    [[ -d /proc/device-tree/soc/busfreq || -d /proc/device-tree/busfreq ]]
}

if ! bat_has_busfreq; then
    echo "Missing busfreq node in device-tree, seems not implemented"
    exit "$BAT_EXITCODE_SKIP"
fi

if rmmod mx6s_capture; then
    mod_mx6s_capture=1
fi

if rmmod mxc_vadc; then
    mod_mxc_vadc=1
fi

save_printk=$(cat /proc/sys/kernel/printk)

#clear dmesg, we don't care about old messages
dmesg -C
#turn on debug prints in dmesg
echo 8 > /proc/sys/kernel/printk

# blank fb
if [[ -f /sys/class/graphics/fb0/blank ]]; then
    echo 1 > /sys/class/graphics/fb0/blank
fi

BUSFREQ_SLEEP_TIME=10

echo "Sleep $BUSFREQ_SLEEP_TIME seconds waiting for busfreq" >&2
$batdir/bat_netpause.sh $BUSFREQ_SLEEP_TIME
echo "Sleep waiting for busfreq over" >&2

busfreq=$(dmesg | grep "Bus freq set to" || true)
busfreq_lines=$(echo "$busfreq" | wc -l)

echo $save_printk > /proc/sys/kernel/printk
if [[ -f /sys/class/graphics/fb0/blank ]]; then
    echo $save_fb_blank > /sys/class/graphics/fb0/blank
fi

if ! [ -z "$mod_mxc_vadc" ]; then
    modprobe mxc_vadc
fi

if ! [ -z "$mod_mx6s_capture" ]; then
    modprobe mx6s_capture
fi

echo -e "Busfreq messages:\n$busfreq"

if [ $busfreq_lines -lt 2 ]; then
    echo "Busfreq did NOT activate"
    exit 1;
else
    echo "Busfreq seems fine"
    exit 0;
fi
