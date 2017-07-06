#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

freqs=($(bat_get_cpu_freqs))
echo "Available CPU frequencies: ${freqs[*]}"

min=${freqs[0]}
max=${freqs[-1]}
echo "Min and max freqs: $min $max"

#force transition to min frequency
if ! bat_wait_cpu_freq $min; then
    echo "current freq ($freq) not minimum ($min_cpu_freq)"
    exit 1
fi

function cleanup
{
    bat_stop_cpu_intensive_task_on_cpu 0
}

trap cleanup EXIT

bat_start_cpu_intensive_task_on_cpu 0

if ! bat_wait_cpu_freq $max; then
    echo "current freq ($freq) not maximum ($min_cpu_freq)"
    exit 1
fi
