#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
echo "Setting governor to ondemand"
cpufreq-set -g ondemand

freqs=($(bat_get_cpu_freqs))
echo "Available CPU frequencies: ${freqs[*]}"

min=${freqs[0]}
max=${freqs[-1]}
echo "Min and max freqs: $min $max"

#force transition to min frequency
if ! bat_wait_cpu_freq $min; then
    echo "current freq ($freq) not minimum ($min)"
    exit 1
else
    echo "current freq ($freq) is minimum ($min)"
fi

function cleanup
{
    bat_stop_cpu_intensive_task_on_cpu 0

    echo "Setting governor to $gov"
    cpufreq-set -g $gov
}

trap cleanup EXIT

bat_start_cpu_intensive_task_on_cpu 0

if ! bat_wait_cpu_freq $max; then
    echo "current freq ($freq) not maximum ($max)"
    exit 1
else
    echo "current freq ($freq) is maximum ($max)"
fi
