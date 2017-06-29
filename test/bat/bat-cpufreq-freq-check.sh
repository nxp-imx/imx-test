#!/bin/bash

function cleanup()
{
    echo "Setting governor to $gov"
    cpufreq-set -g $gov
}

set -e

if ! grep -q userspace /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors; then
    echo "Userspace governor not available?"
    exit 1
fi

gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
echo "Setting governor to userspace"
cpufreq-set -g userspace
trap cleanup EXIT

freq_list=($(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies))
freq_cnt=${#freq_list[@]}

any_failed=0

cpufreq_set_check() {
    local tgt_freq arm_freq
    tgt_freq=$1
    cpufreq-set -f $tgt_freq
    arm_freq=$(egrep "(arm |arm_a7_root_clk)" /sys/kernel/debug/clk/clk_summary | head -n1 | tr -s ' ' | cut -f5 -d ' ')
    if ! [ $((arm_freq/1000)) -eq $tgt_freq ]; then
        echo "Expected ARM freq $tgt_freq actual $((arm_freq/1000))"
        cat /sys/kernel/debug/clk/clk_summary
        any_failed=1
    fi
}

for (( i = 0; i < $freq_cnt; ++i )); do
    cpufreq_set_check "${freq_list[$i]}"
    for (( j = i + 1; j < $freq_cnt; ++j )); do
        echo "cpufreq change from ${freq_list[$i]} to ${freq_list[$j]} and back"
        cpufreq_set_check "${freq_list[$j]}"
        cpufreq_set_check "${freq_list[$i]}"
    done
done

if [[ $any_failed != 0 ]]; then
    echo "Some transitions failed!"
    exit 1
fi
