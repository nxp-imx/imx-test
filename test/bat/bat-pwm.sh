#!/bin/bash

#
# The PWM device driver reduces the amount of power sent to a load
# by varying the width of a series of pulses to the power source.
# One common and effective use of the PWM is controlling the backlight
# of a QVGA panel with a variable duty cycle.
#
# Test that the driver is registered and the correct entries appear
# in sysfs. Try to change backlight brightness and check for errors.
#

set -e

platform=`cat /sys/devices/soc0/soc_id`
case $platform in
i.MX6SX*)
    BACKLIGHT_NAME=backlight1
    ;;
*)
    BACKLIGHT_NAME=backlight
    ;;
esac

BACKLIGHT_SYSFS_PATH=/sys/class/backlight/$BACKLIGHT_NAME

# $1: brightness value to set
function set_brightness
{
    brightness=$1

    echo "Set brightness for $BACKLIGHT_NAME to $brightness"
    echo $brightness > $BACKLIGHT_SYSFS_PATH/brightness
    cur_brightness=$(cat $BACKLIGHT_SYSFS_PATH/brightness)
    if [ $cur_brightness -ne $brightness ]; then
	echo "Could not set brightness  for $BACKLIGHT_NAME to $brightness"
	exit 1
    fi
}

# we should have at least one pwm chip
if ! [ -d /sys/class/pwm/pwmchip0 ]; then
    echo "No PWM chip registered"
    exit 1
fi

# we should have at least one backlight
if ! [ -d $BACKLIGHT_SYSFS_PATH ]; then
    echo "No backlight device registered"
    exit 1
fi

default_brightness=$(cat $BACKLIGHT_SYSFS_PATH/brightness)
max_brightness=$(cat $BACKLIGHT_SYSFS_PATH/max_brightness)

# try to change brightness to 100%
set_brightness $max_brightness

# try to change brightness to an intermediate value
set_brightness $[$max_brightness/2]

# try to change brightness to 0%
set_brightness 0

# change brightness back to default value
set_brightness $default_brightness
