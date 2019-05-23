#! /bin/bash

# This test checks shifting from local percpu time to broadcast timer

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. "$batdir/bat_utils.sh"

if ! [ -d /sys/devices/system/cpu/cpu0/cpuidle/state0 ]; then
    echo "no idle state, not applicable"
    exit $BAT_EXITCODE_SKIP
fi

if bat_running_with_nfsroot; then
    bat_reexec_ramroot "$@"
fi

# Prepare to enter low busfreq
bat_lowbus_prepare
bat_net_down

TEST_CPU_INDEX=0

for sys_cpu in /sys/devices/system/cpu/cpu[0-9]; do
    for sys_cpuidle_state in $sys_cpu/cpuidle/state*; do
        echo 1 > $sys_cpuidle_state/disable
    done
    if [[ `basename $sys_cpu` == "cpu$TEST_CPU_INDEX" ]]; then
        echo 0 > $sys_cpu/cpuidle/state0/disable
    else
        echo 0 > $sys_cpuidle_state/disable
    fi
done

taskset -cp $TEST_CPU_INDEX $$

echo "sleep to enter busfreq..."
sleep 10

# Cycle hotplug on other CPUs
for sys_cpu in /sys/devices/system/cpu/cpu[0-9]; do
    if [[ `basename $sys_cpu` != "cpu$TEST_CPU_INDEX" ]]; then
        echo 0 > $sys_cpu/online
        echo 1 > $sys_cpu/online
    fi
done

echo "sleep 10..."
start_time=$(date +%s)
for i in `seq 10`; do
    # Display real sleep time for each individual sleep.
    echo "sleep 1"
    `which time` sleep 1
done
end_time=$(date +%s)
delta_time=$((end_time - start_time))

# Revert low busfreq
bat_net_restore
bat_lowbus_cleanup

if [[ $delta_time > 15 ]]; then
    echo "Sleeping 10 seconds took $delta_time sec instead"
    exit 1
fi
exit 0
