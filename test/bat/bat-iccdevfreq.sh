#! /bin/bash
#
# This test checks interconnect/devfreq interaction
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. "$batdir/bat_utils.sh"

if [[ $soc_id != i.MX8M* ]]; then
    echo "Only for imx8m right now"
    exit "$BAT_EXITCODE_SKIP"
fi

if bat_running_with_nfsroot; then
    bat_reexec_ramroot "$@"
fi

# cleanup trap
cleanup()
{
    bat_net_restore
    bat_lowbus_cleanup
}
trap cleanup EXIT

# Cycle through all available rates
check_devfreq_set_freq_one()
{
    local devfreq=$1
    local clk_name=$2
    local set_freq=$3
    local device_basename=$(basename "$(readlink "$devfreq/device")")
    local cur_freq clk_rate clk_min_rate

    echo "begin $device_basename set_freq $set_freq"
    #sleep 1
    echo $set_freq > "$devfreq/userspace/set_freq"
    #sleep 2
    cur_freq=$(cat $devfreq/cur_freq)
    clk_rate=$(bat_get_clk_rate $clk_name)

    echo "after $device_basename set_freq $set_freq: cur_freq $cur_freq clk_rate $clk_rate"

    # cur_freq should always match clk_rate
    if [[ $cur_freq != $clk_rate ]]; then
        echo "bad: clk rate $clk_rate != dev freq $cur_freq"
        let ++fail
    fi

    # cur_freq should be max(set_freq, clk_min_rate)
    clk_min_rate=$(cat /sys/kernel/debug/clk/$clk_name/clk_min_rate)
    if [[ $set_freq -lt $clk_min_rate ]]; then
        if [[ $cur_freq != $clk_min_rate ]]; then
            echo "bad: dev rate $cur_freq not min $clk_min_rate"
            let ++fail
        fi
    else
        if [[ $cur_freq != $set_freq && $cur_freq != $((set_freq + 1)) ]]; then
            echo "bad: dev rate $cur_freq not set $set_freq"
            let ++fail
        fi
    fi
}

check_devfreq_set_freq()
{
    local devfreq clk_name
    local device_basename
    local set_freq

    devfreq=$(readlink -f "$1") || {
        echo "missing $1"
        exit 1
    }
    clk_name=$2
    device_basename=$(basename "$(readlink "$devfreq/device")")

    if ! grep -q userspace $devfreq/available_governors; then
        echo "$device_basename: userspace governor not available only $(cat "$devfreq/available_governors")"
        return
    fi
    old_governor=$(cat "$devfreq/governor")
    echo userspace > "$devfreq/governor"

    echo "check $(basename $devfreq) device $device_basename clk $clk_name"

    # Check all possible transitions:
    local freq1 freq2
    for freq1 in $(cat "$devfreq/available_frequencies"); do
        check_devfreq_set_freq_one $devfreq $clk_name $freq1
        for freq2 in $(cat "$devfreq/available_frequencies"); do
            if [[ $freq1 != $freq2 ]]; then
                check_devfreq_set_freq_one $devfreq $clk_name $freq2
            fi
        done
    done

    # Default old governor is userspace but can't reset set_freq :(
    echo powersave > "$devfreq/governor"
    echo $old_governor > "$devfreq/governor"
}

check_opt_devfreq_set_freq()
{
    local devfreq
    if devfreq=$(ls -d /sys/bus/platform/devices/$1/devfreq/*); then
        check_devfreq_set_freq $devfreq $2
    else
        echo "ignore missing /sys/bus/platform/devices/$1"
    fi
}

check_soc_devfreq()
{
    # currently assumes imx8mm
    local dram_clk
    dram_clk=$(bat_find_clk dram dram_core dram_core_clk)
    check_opt_devfreq_set_freq *.memory-controller $dram_clk
    check_opt_devfreq_set_freq 32700000.interconnect noc
}

bat_lowbus_prepare
bat_net_down
if ! bat_wait_busfreq_low; then
    echo "failed to check busfreq low rate"
    exit $BAT_EXITCODE_FAIL
fi

# check after lowbus
fail=0
check_soc_devfreq

if [[ $fail -eq 0 ]]; then
    echo "devfreq test OK" >&2
else
    echo "fail count: $fail" >&2
fi
[[ $fail -eq 0 ]];
