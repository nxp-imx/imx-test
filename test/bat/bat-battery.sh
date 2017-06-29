#!/bin/bash

#
# Battery Charging is supported by the max8903-charger on some
# i.MX 6 SABRE SD boards.
#
# This test checks that the battery driver registered properly
# on these boards and that the sysfs interface exists and reports
# the battery is present.
#

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

set -e

BATTERY_PATH=/sys/class/power_supply

platform=`cat /sys/devices/soc0/soc_id`
case $platform in
i.MX6Q*|i.MX6QP*|i.MX6SL)
    if grep -qi automotive /sys/firmware/devicetree/base/model; then
        echo "Automotive platforms do not have battery"
        exit $BAT_EXITCODE_SKIP
    fi
    echo "Platform '$platform' has battery max8903"
    ;;
*)
    echo "Platform '$platform' does not have battery"
    exit 2
    ;;
esac

# we should have 3 power supplies registered:
# DC charger, USB charger and battery
if ! [ -d $BATTERY_PATH/max8903-charger ]; then
    echo "No max8903-charger power supply registered"
    exit 1
fi

if ! [ -d $BATTERY_PATH/max8903-ac ]; then
    echo "No max8903-ac power supply registered"
    exit 1
fi

if ! [ -d $BATTERY_PATH/max8903-usb ]; then
    echo "No max8903-usb power supply registered"
    exit 1
fi

type=$(cat $BATTERY_PATH/max8903-charger/type)
if [ "$type" != "Battery" ]; then
    echo "max8903-charger is not a Battery"
    exit 1
fi

present=$(cat $BATTERY_PATH/max8903-charger/present)
if [ $present -ne 1 ]; then
    echo "max8903-charger battery not present"
    exit 1
fi

status=$(cat $BATTERY_PATH/max8903-charger/status)
capacity=$(cat $BATTERY_PATH/max8903-charger/capacity)
capacity_level=$(cat $BATTERY_PATH/max8903-charger/capacity_level)
health=$(cat $BATTERY_PATH/max8903-charger/health)
voltage_now=$(cat $BATTERY_PATH/max8903-charger/voltage_now)

echo -e "Battery max8903-charger:\n"\
     " status: $status\n"\
     " capacity: $capacity\n"\
     " capacity_level: $capacity_level\n"\
     " health: $health\n"\
     " voltage_now: $voltage_now\n"
