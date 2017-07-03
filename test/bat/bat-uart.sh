#!/bin/bash

#
# Tests for the uart ports:
#  - check that the uart ports are successfully registered
#  - check that data is correctly transmitted and received in loopback mode
#  - check that multiple bytes data is correctly transmitted and received in
# loopback mode for various baud rates
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))

declare -A stress_test

machine=`cat /sys/devices/soc0/machine`
case $machine in
'Freescale i.MX6 UltraLite 14x14 EVK Board'|\
'Freescale i.MX6 ULL 14x14 EVK Board')
    uart_ports="ttymxc0 ttymxc1"
    ;;
"Freescale i.MX6 SoloX SDB RevB Board")
    uart_ports="ttymxc0 ttymxc4"
    ;;
'Freescale i.MX7 SabreSD Board'|\
'Freescale i.MX7D SabreSD Board')
    uart_ports="ttymxc0 ttymxc4 ttymxc5"
    ;;
'Freescale i.MX6 Quad SABRE Automotive Board'|\
'Freescale i.MX6 Quad Plus SABRE Automotive Board'|\
'Freescale i.MX6 DualLite/Solo SABRE Automotive Board')
    uart_ports="ttymxc2 ttymxc3"
    # RXD pin is shared with NAND (see MLK-12482)
    stress_test["ttymxc2"]="disable"
    ;;
'NXP i.MX7ULP EVK'|\
'Freescale i.MX8QM ARM2')
    uart_ports="ttyLP0"
    ;;
*)
    uart_ports="ttymxc0"
    ;;
esac

function cleanup
{
    if [ "$pids" != ""  ]; then
        echo "Resume processes using /dev/${port}: $pids"
        signal_processes $pids CONT
    fi
}

# returns list of pids that are using file $1
# $1: file name to search for
function lsof()
{
    filename="$1";

    all_pids=$(find /proc -maxdepth 1 -name "[0-9]*")
    for pid in $all_pids; do
        uses_file=$(ls -l ${pid}/fd 2>/dev/null | grep "${filename}")
        if [ "$uses_file" != "" ]; then
            echo "${pid#/proc/}"
        fi
    done
}

# sends signal $2 to given list of processes $1
# $1: list of pids
# $2: signal
function signal_processes()
{
    pids="$1"
    signal="$2"

    for pid in $pids; do
        kill -${signal} $pid
    done
}

current_pid=$$
test_baud_rates="9600 19200 115200 576000 1152000 3000000"

# Check that the uart dev nodes exist
echo "Test: check if $machine has uart ports $uart_ports"
for port in $uart_ports; do
    if ! [ -e /dev/${port} ]; then
        echo "No device /dev/${port}"
        exit 1
    fi
done

# Make sure we restore stopped processes at error
trap cleanup EXIT

# Transfer data in loopback mode
for port in $uart_ports; do
    pids=$(lsof /dev/${port})

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
        signal_processes $pids STOP
	# disable stress test for console uart
	stress_test[$port]="disable"
    fi

    # Run simple loopback test
    echo "Test: loopback test for /dev/${port}"
    $batdir/../UART/mxc_uart_test.out /dev/${port}

    # Run test with various baud rates. Don't use more then FIFO size
    # chunks as the loopback test does not use flow control.
    if [ "${stress_test[$port]}" != "disable" ]; then
	for baud in $test_baud_rates; do
            echo "Test: loopback test for /dev/${port} at baud $baud"
            $batdir/../UART/mxc_uart_stress_test.out /dev/${port} $baud D L 5 31 N
	done
    fi

    # resume processes using this uart
    if [ "$pids" != ""  ]; then
        echo "Resume processes using /dev/${port}: $pids"
        signal_processes $pids CONT
    fi
done
