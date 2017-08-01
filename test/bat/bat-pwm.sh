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

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. "$batdir/bat_utils.sh"

# $1: backlight name
# $2: brightness value to set
function set_brightness
{
    backlight=$1
    backlight_sysfs_path=/sys/class/backlight/$backlight
    brightness=$2

    echo "Set brightness for $backlight to $brightness"
    echo $brightness > $backlight_sysfs_path/brightness
    cur_brightness=$(cat $backlight_sysfs_path/brightness)
    if [ $cur_brightness -ne $brightness ]; then
	echo "Could not set brightness for $backlight to $brightness"
	exit 1
    fi
}

backlight_test() {
    backlight=$1
    backlight_sysfs_path=/sys/class/backlight/$backlight

    echo "Testing backlight '$backlight'"

    default_brightness=$(cat $backlight_sysfs_path/brightness)
    max_brightness=$(cat $backlight_sysfs_path/max_brightness)

    # try to change brightness to 100%
    set_brightness $backlight $max_brightness

    # try to change brightness to an intermediate value
    set_brightness $backlight $((max_brightness / 2))

    # try to change brightness to 0%
    set_brightness $backlight 0

    # change brightness back to default value
    set_brightness $backlight $default_brightness
}

# we should have at least one pwm chip
if ! [ -d /sys/class/pwm/pwmchip0 ]; then
    echo "No PWM chip registered"
    exit 1
fi

BACKLIGHT_LIST=$(ls /sys/class/backlight)

# we should have at least one backlight
if [ -z "$BACKLIGHT_LIST" ]; then
    echo "No backlight device registered"
    exit $BAT_EXITCODE_SKIP
fi

for backlight in $BACKLIGHT_LIST; do
    backlight_test "$backlight"
done
