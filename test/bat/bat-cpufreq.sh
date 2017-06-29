#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

bat_get_cpu_freqs

#force transition to min frequency
if ! bat_wait_min_cpu_freq; then
    echo "current freq ($freq) not minimum ($min_cpu_freq)"
    exit 1
fi

function cleanup
{
    bat_stop_cpu_intensive_task_on_cpu 0
}

trap cleanup EXIT

bat_start_cpu_intensive_task_on_cpu 0

if ! bat_wait_max_cpu_freq; then
    echo "current freq ($freq) not maximum ($min_cpu_freq)"
    exit 1
fi
