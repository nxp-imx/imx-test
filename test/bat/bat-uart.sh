#!/bin/bash

#
# Tests for the uart ports:
#  - check that data is correctly transmitted and received in loopback mode
#  - check that multiple bytes data is correctly transmitted and received in
# loopback mode for various baud rates
#
# This test tries to be board-independent
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

declare -A stress_test

# RXD pin is shared with NAND (see MLK-12482)
machine=`cat /sys/devices/soc0/machine`
case $machine in
"Freescale i.MX6 "*" SABRE Automotive Board")
    stress_test["ttymxc2"]="disable"
    ;;
esac

function cleanup
{
    if [ "$pids" != ""  ]; then
        echo "Resume processes using /dev/${port}: $pids"
        kill -s CONT $pids
    fi
}

# returns list of pids that are using file $1
# $1: file name to search for
function lsof()
{
    filename="$1";

    all_pids=$(find /proc -maxdepth 1 -name "[0-9]*")
    for pid in $all_pids; do
        if ls -l ${pid}/fd 2>/dev/null | grep -q "${filename}"; then
            echo "${pid#/proc/}"
        fi
    done
}

current_pid=$$
test_baud_rates="9600 19200 115200 576000 1152000 3000000"

# Test on all uart
uart_ports=$(find /sys/class/tty \( -iname ttymxc* -o -iname ttyLP* \) -printf '%f\n')

# Make sure we restore stopped processes at error
trap cleanup EXIT

# Transfer data in loopback mode
for port in $uart_ports; do
    pids=$(lsof /dev/${port})

    driver=$(basename $(readlink -f "/sys/class/tty/$port/device/driver"))
    echo "checking uart $port driver $driver"

    # Don't run test from serial console
    for pid in $pids; do
        if [ "$current_pid" == "$pid" ]; then
           echo "Cannot test port /dev/${port} while using it as console."\
                "Run test using SSH."
           trap - EXIT
           exit 1
        fi
    done

    # pause processes using this uart
    if [ "$pids" != ""  ]; then
        echo "Pause processes using /dev/${port}: $pids"
        kill -s STOP $pids
	# disable stress test for console uart
	stress_test[$port]="disable"
    fi

    # Run simple loopback test
    echo "Test: loopback test for /dev/${port}"
    $batdir/../UART/mxc_uart_test.out /dev/${port}

    # Run test with various baud rates. Don't use more then FIFO size
    # chunks as the loopback test does not use flow control.
    if [ "${stress_test[$port]}" != "disable" -a "$driver" == "imx-uart" ]; then
	for baud in $test_baud_rates; do
            echo "Test: loopback test for /dev/${port} at baud $baud"
            $batdir/../UART/mxc_uart_stress_test.out /dev/${port} $baud D L 5 31 N
	done
    fi

    # resume processes using this uart
    if [ "$pids" != ""  ]; then
        echo "Resume processes using /dev/${port}: $pids"
        kill -s CONT $pids
    fi
done
