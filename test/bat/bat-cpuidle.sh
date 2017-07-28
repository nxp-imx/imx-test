#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

# we should have at least one idle state
if ! [ -d /sys/devices/system/cpu/cpu0/cpuidle/state0 ]; then
    socid=$(cat /sys/devices/soc0/soc_id)
    if [ "$socid" = "i.MX8QXP" ]; then
	echo "Skipping, $socid not supported yet"
	exit $BAT_EXITCODE_SKIP
    fi
    echo "no idle state"
    exit 1
fi

taskset -cp 0 $$

# test 1 - avoid idle

# Run a cpu-intensive background task
bat_start_cpu_intensive_task_on_cpu 0

sleep 1

for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    declare old_time_$bi=$(cat $i/time)
done

sleep 1

for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    declare new_time_$bi=$(cat $i/time)
done

echo -n "killing busy job..."
bat_stop_cpu_intensive_task_on_cpu 0
echo "dead!"

# check that no idle state has been entered
for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    old=old_time_$bi
    new=new_time_$bi
    delta=$[${!new}-${!old}]
    echo "busy time delta for state $(cat $i/name): $delta"
    if [ $delta -gt 0 ]; then
        echo "should not have gone idle!\n"
	exit 1
    fi
done


# test 2 - allow idle
for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    declare old_time_$bi=$(cat $i/time)
done

sleep 2

for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    declare new_time_$bi=$(cat $i/time)
done

# check that at least one cpuidle state has been entered
not_idle=0
for i in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
    bi=$(basename $i)
    old=old_time_$bi
    new=new_time_$bi
    delta=$[${!new}-${!old}]
    echo "idle time delta for state $(cat $i/name): $delta"
    if [ $delta -gt 0 ]; then
	not_idle=1
    fi
done

if [ $not_idle -eq 0 ]; then
    echo "should have gone idle!\n"
    exit 1;
fi
