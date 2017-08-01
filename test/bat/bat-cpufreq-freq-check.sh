#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

set -e

function cleanup()
{
    echo "Setting governor to $gov"
    cpufreq-set -g $gov
}

function get_cpuinfo()
{
    echo $(grep "$1" /proc/cpuinfo | head -n$(($2+1)) | tail -n1 | cut -f2 -d: | tr -d ' ')
}

get_arm_clk_name()
{
    cpu_arch=$(get_cpuinfo "CPU architecture" $1)
    cpu_part=$(get_cpuinfo "CPU part" $1)

    if [ "$cpu_arch" = "8" ]; then
        if [ "$cpu_part" = "0xd08" ]; then
            echo "a72_div";
        elif [ "$cpu_part" = "0xd03" ]; then
            if [ -d /sys/kernel/debug/clk/arm_a53_div ]; then
                echo "arm_a53_div"
            else
                echo "a53_div"
            fi
        elif [ "$cpu_part" = "0xd04" ]; then
            echo "a35_div"
        else
            echo "Unknown ARMv8 part $cpu_part, could not find clock" >&2
            return 1
        fi
    else
        for name in arm arm_a7_root_clk; do
            if [[ -d /sys/kernel/debug/clk/$name ]]; then
                echo $name
                return 0
            fi
        done
        echo "Could not find ARMv7 clock" >&2
        return 1
    fi
}

cpufreq_set_check()
{
    local tgt_freq arm_freq
    tgt_freq=$1
    cpufreq-set -f $tgt_freq -c $2
    clk_name=$(get_arm_clk_name $2)
    clk_rate=$(bat_get_clk_rate $clk_name)
    if ! [ $(($clk_rate/1000)) -eq $tgt_freq ]; then
        echo "Expected ARM freq $tgt_freq actual: $clk_name=$((clk_rate/1000))"
        failed=$(($failed+1))
    fi
}

gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
echo "Setting governor to userspace"
cpufreq-set -g userspace
trap cleanup EXIT

ncpus=$(grep processor /proc/cpuinfo | wc -l)
failed=0

for((cpu=0;cpu<ncpus;cpu++)); do
    freq_list=($(bat_get_cpu_freqs $cpu))
    freq_cnt=${#freq_list[@]}

    for (( i = 0; i < $freq_cnt; ++i )); do
        cpufreq_set_check "${freq_list[$i]}" $cpu
        for (( j = i + 1; j < $freq_cnt; ++j )); do
            echo "cpufreq change on cpu$cpu from ${freq_list[$i]} to ${freq_list[$j]} and back"
            cpufreq_set_check "${freq_list[$j]}" $cpu
            cpufreq_set_check "${freq_list[$i]}" $cpu
        done
    done
done

if [[ $failed != 0 ]]; then
    echo "$failed transitions failed!"
    cat /sys/kernel/debug/clk/clk_summary
    exit 1
fi
