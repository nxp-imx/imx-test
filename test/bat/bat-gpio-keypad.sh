#!/bin/bash

#
# Tests that the GPIO keys are successfully registered for each board.
# Most boards have a power on/off button, some also have volume up/down, etc.
# Also tests that the state of the GPIO keys is unpressed when running this
# test.
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

machine=$(cat /sys/devices/soc0/machine)
case $machine in
'Freescale i.MX6 SoloLite EVK Board')
    gpio_possible_devs="kpp"
    ;;
esac

gpio_possible_devs="snvs-powerkey gpio-keys rpmsg-keys $gpio_possible_devs"

# find supported devs from the list of possible gpio devs
function find_supported_devices
{
    devs=""

    for dev in $gpio_possible_devs; do
        path=$(find /sys/firmware/devicetree/base -name "$dev" -o -name "$dev@?*")
        if [ "$path" != "" ]; then
            devs="$devs $dev"
        fi
    done

    echo $devs
}

# find event id from sysfs
# $1: event name
function find_event
{
    name="$1"

    input_devs=$(find /sys/class/input -name "input[0-9]*")
    for input_dev in $input_devs; do
        if [ $(grep "$name" $input_dev/name) ]; then
            event_path=$(ls -d $input_dev/event*)
            event_name=$(basename $event_path)
            event_id=${event_name//[^0-9]/}
            echo $event_id
        fi
    done
}

# find supported keycodes
# $1: gpio dev name
function find_supported_keycodes
{
    dev=$1
    keycodes=""

    path=$(find /sys/firmware/devicetree/base -name "$dev" -o -name "$dev@?*")

    # search for keycodes from linux,keycode
    keycodes_path=$(find $path -name linux,keycode -o -name linux,code)
    for keycode_path in $keycodes_path; do
        code=$(cat $keycode_path)
        code_hex=$(echo -ne $code | hexdump -v  -e '/1 "%02X"')
        code_dec=$(echo "ibase=16;obase=A;${code_hex}" | bc)
        keycodes="$keycodes $code_dec"
    done

    # search for keycodes from linux,keymap
    keycodes_path=$(find $path -name linux,keymap)
    for keycode_path in $keycodes_path; do
        # linux,keypad contains a list of 32-bit integer values
        # that map a key matrix; each 32-bit integer value maps
        # row and column in the first 16 bits and the actual keycode
        # in the last 16 bits.
        codes_hex=$(cat $keycode_path | hexdump -v -e '/1 "%02X\n"' | \
                    awk 'BEGIN { i=0; } { if (i % 4 == 2) printf $1; \
                    if (i % 4 == 3) printf $1"\n"; i++ }')
        for code_hex in $codes_hex; do
            code_dec=$(echo "ibase=16;obase=A;${code_hex}" | bc)
            keycodes="$keycodes $code_dec"
        done
    done

    echo $keycodes
}

# check the status of the gpio key (pressed, unpressed, error)
# $1: event id
# $2: key type (e.g. EV_KEY)
# $3: key value (e.g. KEY_POWER, KEY_VOLUMEDOWN)
function check_key_unpressed
{
    event_id=$1
    key_type=$2
    key_value=$3

    # evtest will return 0 if key is unpressed and 10 if key is pressed
    set +e
    evtest --query /dev/input/event${event_id} ${key_type} ${key_value}
    ret=$?
    set -e
    if [ $ret -eq 0 ]; then
        echo "/dev/input/event${event_id}, ${key_type}, ${key_value}: unpressed"
    elif [ $ret -eq 10 ]; then
        echo "/dev/input/event${event_id}, ${key_type}, ${key_value}: "\
             "invalid state: pressed"
        exit 1
    else
        echo "/dev/input/event${event_id}, ${key_type}, ${key_value}: "\
             "invalid state: $ret"
        exit 1
    fi
}

gpio_devs=$(find_supported_devices)
if [ "$gpio_devs" == ""  ]; then
    echo "Missing gpio keypad support"
    exit 1
fi

for dev in $gpio_devs; do
    echo "Testing GPIO dev \"$dev\""

    event_id=$(find_event $dev)
    if [ "$event_id" == "" ]; then
        echo "GPIO dev $dev not found"
        exit 1
    fi
    echo "GPIO dev $dev is registered as /dev/input/event${event_id}"

    evtest /dev/input/event${event_id} &
    sleep 0.1
    pkill evtest

    key_values=$(find_supported_keycodes $dev)
    if [ "$key_values" == "" ]; then
        echo "No supported keycodes found for dev $dev"
        exit 1
    fi

    for key_value in $key_values; do
        check_key_unpressed ${event_id} EV_KEY ${key_value}
    done
done
