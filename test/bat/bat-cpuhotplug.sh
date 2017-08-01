#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

procs=$(grep processor /proc/cpuinfo | wc -l)

for((i=1;i<=$procs-1;i++)); do
    echo 0 > /sys/devices/system/cpu/cpu$i/online
done

c_procs=$(grep processor /proc/cpuinfo | wc -l)
if ! [ $c_procs -eq 1 ]; then
    echo "Unable to shutdown some CPUs"
    cat /proc/cpuinfo
    for((i=1;i<=$procs-1;i++)); do
        echo 1 > /sys/devices/system/cpu/cpu$i/online
    done
    exit 1
fi

for((i=1;i<=$procs-1;i++)); do
    echo 1 > /sys/devices/system/cpu/cpu$i/online
done

c_procs=$(grep processor /proc/cpuinfo | wc -l)
if ! [ $c_procs -eq $procs ]; then
    echo "Unable to wakeup some CPUs"
    exit 1
fi
