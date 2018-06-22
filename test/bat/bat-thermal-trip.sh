#!/bin/bash

#
# The thermal driver will monitor the SoC temperature in a certain frequency.
# It defines two trip points: critical and passive. Cooling device will take
# action to protect the SoC according to the different trip points that SoC
# has reached:
# - When reaching critical point, cooling device will shut down the system.
# - When reaching passive point, cooling device will lower CPU frequency and
# notify GPU to run at a lower frequency.
# - When the temperature drops to 10 °C below passive point, cooling device
# will release all the cooling actions
#
# This test is setting a test passive trip point, then runs a cpu-intensive
# task to increase SoC temperature. When reaching passive trip point, the
# cooling devices must be activated and CPU frequency lowered. After restoring
# the original passive trip point, the cooling devices must be deactivated.
#
# Testing critical trip point is not possible due to the need to reboot.

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

function cleanup
{
    # kill cpu-intensive tasks
    bat_stop_cpu_intensive_task_on_all_cpus

    # restore initial trip point
    echo "Restore passive trip point to $tp_passive_temp_init"
    echo $tp_passive_temp_init > $tp_passive_temp_file

    # wait for trip point to be deactivated
    sleep 1
}

THERMAL_ZONE_PATH=/sys/class/thermal/thermal_zone0

# we should have at least one thermal zone
if ! [ -d $THERMAL_ZONE_PATH ]; then
    echo "No thermal zone registered"
    exit 1
fi

# List cooling devices
if [[ -z `find /sys/class/thermal/ -name 'cooling_device*'` ]]; then
    echo "No cooling device registered"
    exit 1
fi
for cooling_device in /sys/class/thermal/cooling_device*; do
    echo "Found $cooling_device type `cat $cooling_device/type`"
done

# find passive and critical trip point temp
tp_id=0
while [ -f $THERMAL_ZONE_PATH/trip_point_${tp_id}_type ]; do
    tp_type=$(cat $THERMAL_ZONE_PATH/trip_point_${tp_id}_type)
    tp_temp_file=$THERMAL_ZONE_PATH/trip_point_${tp_id}_temp
    tp_temp=$(cat $tp_temp_file)
    if [ "$tp_type" == "passive" ]; then
        tp_passive_temp_file=$tp_temp_file
        tp_passive_temp_init=$tp_temp
    elif [ "$tp_type" == "critical" ]; then
        tp_critical_temp_file=$tp_temp_file
        tp_critical_temp_init=$tp_temp
    fi
    tp_id=$[$tp_id+1]
done

if [ "$tp_passive_temp_init" == "" -o "$tp_critical_temp_init" == "" ]; then
    echo "No passive/critical trip point set"
    exit 1
fi

# initially we need to have cooling devices in state 0
for cooling_device in /sys/class/thermal/cooling_device*; do
    if [[ `cat $cooling_device/cur_state` -ne 0 ]]; then
        echo "Cannot run test: `basename $cooling_device` already enabled"
        exit 1
    fi
done

# Make sure we restore original trip point when exiting
trap cleanup EXIT

#
# 1. Test passive trip point
#

# get current temperature
temp=$(bat_read_temp)
echo "Current temp $temp"

freqs=($(bat_get_cpu_freqs))
echo "Available CPU frequencies: ${freqs[*]}"

min=${freqs[0]}
max=${freqs[-1]}
echo "Min and max freqs: $min $max"

echo "Run cpu-intensive task on all cpus ($(nproc) cpus) ..."
# force increase the cpu temp
bat_start_cpu_intensive_task_on_all_cpus

if ! bat_wait_cpu_freq $max; then
    echo "warning: cpu freq not maximum ($freq)"
fi
freq_high=$freq

THERMAL_TRIP_DELTA=-1000

# Change passive trip point to a lower then current temperature to
# force a passive trip point
tp_passive_temp=$(( $temp + $THERMAL_TRIP_DELTA ))
echo "Set passive trip point temp to $tp_passive_temp"
echo $tp_passive_temp > $tp_passive_temp_file

pending_freq_lowered=1
if [ $min -eq $max ]; then
    pending_freq_lowered=0
fi
pending_cooling_device=1

# wait for trip point to activate while keeping cpu busy
for ((sec=0; sec<20; sec++)); do
    new_temp=$(bat_read_temp)
    echo "Curent temp $new_temp (delta $((new_temp - temp)))"

    if [[ $pending_freq_lowered == 1 ]]; then
        freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
        if [ $freq -lt $freq_high ]; then
            echo "CPU frequency lowered from $freq_high to $freq"
            pending_freq_lowered=0
        fi
    fi

    if [[ $pending_cooling_device == 1 ]]; then
        for cooling_device in /sys/class/thermal/cooling_device*; do
            cur_state=`cat $cooling_device/cur_state`
            max_state=`cat $cooling_device/max_state`
            if [[ `cat $cooling_device/cur_state` -ne 0 ]]; then
                echo "`basename $cooling_device` type `cat $cooling_device/type` enabled in state $cur_state/$max_state"
                pending_cooling_device=0
            fi
        done
    fi

    # Check if all done
    if [[ $pending_freq_lowered == 0 && $pending_cooling_device == 0 ]]; then
        break
    fi

    sleep 1
done

# check that cooling devices have been activated
if [[ $pending_cooling_device == 1 ]]; then
    echo "Cooling devices are not enabled after passing passive trip point"
    exit 1
fi

# check that cpu frequency was lowered
if [[ $pending_freq_lowered == 1 ]]; then
    echo "CPU frequency $freq has not been decreased from $freq_high "\
         "after passing passive trip point"
    exit 1
fi

