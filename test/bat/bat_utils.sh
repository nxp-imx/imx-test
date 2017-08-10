#! /bin/bash

BAT_EXITCODE_SKIP=2
BAT_EXITCODE_TODO=3

# skip not yet implemented features or not yet fixed bugs
# (for unsupported features use the skip array below)
declare -A todo=(\
["i.MX6SLL"]="bat-busfreq.sh" \
["i.MX8QXP"]="bat-audio.sh bat-cpuidle.sh bat-suspend.sh bat-thermal-trip.sh" \
["i.MX8QM"]="bat-audio.sh bat-suspend.sh bat-thermal-trip.sh" \
["i.MX8MQ"]="bat-uart.sh" \
)

# skip unsupported features
declare -A skip=(\
["i.MX8QXP"]="bat-gpio-keypad.sh bat-pwm.sh" \
["i.MX8QM"]="bat-gpio-keypad.sh bat-pwm.sh" \
["i.MX8MQ"]="bat-pwm.sh" \
)

# Check if running with nfsroot
bat_running_with_nfsroot() {
    cat /proc/cmdline|grep -q root=/dev/nfs
}

# skip if running with nfsroot
bat_skip_with_nfsroot() {
    if bat_running_with_nfsroot; then
        echo "Running with NFS!"
        exit $BAT_EXITCODE_SKIP
    fi
}

# reexecute the current script ($0) inside a temporary tmpfs root
#
# this makes it possible to run scripts which disable the network from a nfsroot
#
# This function does if already inside a ramroot or returns the exit code from
# the re-executed script.
bat_reexec_ramroot() {
    RAMROOT=/ramroot
    if [[ $0 == /bin/* ]]; then
        pr_debug "running inside ramroot"
    else
        pr_debug "reexec $0 inside ramroot..."
        # reset
        umount -R "$RAMROOT" 2>/dev/null || true
        rm -rf "$RAMROOT"

        # create
        mkdir -p $RAMROOT
        mount -t tmpfs none $RAMROOT
        mkdir -p $RAMROOT/bin $RAMROOT/lib
        for fs in proc sys dev; do
            mkdir -p "$RAMROOT/$fs"
            mount -o bind "/$fs" "$RAMROOT/$fs"
        done

        local batdir
        batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))

        # copy
        {
            which bash
            which busybox
            which dmesg
            which ip
            which ps
            which strace
            which ping
            which rtcwake
            which sleep
            which date
            which time
            which taskset
            echo "$batdir/bat_utils.sh"
            echo "$0"
        } | {
            while read -r item; do
                cp $item $RAMROOT/bin
                if file -L $item | grep -q 'dynamically linked'; then
                    ldd $item | {
                        while read -r a b c rest; do
                            if [[ $b == '=>' ]]; then
                                libpath=$c
                            else
                                libpath=$a
                            fi
                            [[ $libpath == linux-vdso* ]] && continue
                            cp -n "$libpath" $RAMROOT/lib
                        done
                    }
                fi
            done
        }
        # Our version of busybox is blessed with the lack of --install
        for applet in ls stat echo cat grep wc find sh readlink dirname which mount dd seq basename; do
            ln -sf busybox $RAMROOT/bin/$applet
        done

        set +e
        chroot "$RAMROOT" "/bin/`basename $0`" $@
        chroot_exec_status=$?
        pr_debug "cleaning up ramroot"
        umount -R "$RAMROOT"
        rm -rf "$RAMROOT"
        exit $chroot_exec_status
    fi
}

bat_net_down()
{
    bat_eth0_status=$(ip link show up | grep eth0 | wc -l)
    bat_eth1_status=$(ip link show up | grep eth1 | wc -l)
    bat_eth0_saved_route=$(ip route show | grep '^default' | grep eth0 || true)

    if [ $bat_eth0_status -gt 0 ]; then
        ip link set eth0 down
    fi
    if [ $bat_eth1_status -gt 0 ]; then
        ip link set eth1 down
    fi
}

bat_net_restore()
{
    if [ $bat_eth0_status -gt 0 ]; then
        ip link set eth0 up
    fi
    if [[ -n $bat_eth0_saved_route ]]; then
        if ! ip route show | grep '^default' | grep eth0; then
            ip route add $bat_eth0_saved_route
        fi
    fi
    if [ $bat_eth1_status -gt 0 ]; then
        ip link set eth1 up
    fi
}

pr_debug()
{
    echo "$@" >&2
}

kernel_is_version()
{
    version=$(cat /proc/version | sed -s 's/Linux version \([1-9][.][0-9]*[.][0-9]*\).*/\1/')
    if [[ $version == $1* ]]; then
        return 0
    else
        return 1
    fi
}

# Run memtool
bat_memtool() {
    "${batdir}/../memtool" "$@"
}

# Run memtool and return register value. Includes 0x but nothing else
bat_memtool_reg_value() {
    bat_memtool "$@" | sed -ne 's/^.*Value:\(0x[0-9a-fA-F]\+\) .*$/\1/pg'
}

# Read memtool hex dump values from an addr
# Returns a stream of values with 0x prepended.
bat_memtool_addr_value() {
    # First search for headers and print date
    # Then prepend 0x to individual data value and put everything on single line
    bat_memtool "$@" \
        | sed -ne 's/^0x[0-9a-fA-F]\+: \(.*\)$/\1/p' \
        | sed -e 's/^ /0x/g' -e 's/ /\n0x/g'
}

# Quick assertion. Example:
#
# bat_assert_equal expected actual
# bat_assert_equal expected actual "blah blah"
bat_assert_equal()
{
    if [[ $1 != $2 ]]; then
        echo "Assertion error: '$1' != '$2'"
        if [[ $# -gt 2 ]]; then
            shift 2
            echo -n ': '"$@"
        fi
        exit 1
    fi
}

# Start cpu intensive task on cpu $1
#
# $1: cpu id to run task
bat_start_cpu_intensive_task_on_cpu()
{
    local cpu_id=$1

    taskset -c $cpu_id dd if=/dev/urandom of=/dev/null bs=1M &
    declare -g pid_cpu_$cpu_id=$!
}

# Stop cpu intensive task on cpu $1
#
# $1: cpu id to run task
bat_stop_cpu_intensive_task_on_cpu()
{
    local cpu_id=$1

    local pid_name=pid_cpu_$cpu_id
    local pid_value=${!pid_name}
    if [ "$pid_value" != "" ]; then
        kill -9 $pid_value
    fi
}

# Start cpu intensive task on all cpus
bat_start_cpu_intensive_task_on_all_cpus()
{
    local ncpus=$(nproc)

    for ((i=0; i<ncpus; i++)); do
        bat_start_cpu_intensive_task_on_cpu $i
    done
}

# Stop cpu intensive task on all cpus
bat_stop_cpu_intensive_task_on_all_cpus()
{
    local ncpus=$(nproc)

    for ((i=0; i<ncpus; i++)); do
        bat_stop_cpu_intensive_task_on_cpu $i
    done
}

bat_get_cpu_freqs()
{
    cpu=${1:-0}
    local freqs=$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_available_frequencies)
    if [ $(echo $freqs | wc -w) -lt 1 ]; then
        echo "no available frequencies" >&2
        return 1;
    fi
    echo "$freqs"
}

bat_wait_cpu_freq()
{
    local wait_freq=${1}
    secs=${2:-10}
    cpu=${3:-0}

    for((i=0;i<$secs;i++)); do
        sleep 1
        freq=$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_cur_freq)
        if [ $freq -eq $wait_freq ]; then
            return 0
        fi
        echo "Still waiting for freq $wait_freq current scaling_cur_freq=$freq"
        ps axr
    done
    return 1
}

# Return 0 if a certain KCONFIG variable is available, otherwise 1
# Need to provide full name of config option
bat_kconfig_enabled()
{
    if ! [[ -f /proc/config.gz ]]; then
        echo "Missing /proc/config.gz; please enable CONFIG_IKCONFIG_PROC"
        return 0
    fi
    [[ -n `zcat /proc/config.gz | grep "^$1=\(y\|m\)$"` ]]
}

# According to the temp driver, it may require up to ~17us to complete
# a measurement. The driver waits for about 50 us on most boards and
# 20 ms on imx 7D. For this reason, reading temperature can sometimes
# fail with "Resource temporarily unavailable" (especially when done in
# a loop). If we get errors when reading the temp, we retry a couple of
# times.
bat_read_temp() {
    local thermal_zone temp retries

    thermal_zone=${1:-/sys/class/thermal/thermal_zone0}
    retries=3

    if ! [ -d $thermal_zone ]; then
        echo "missing $thermal_zone directory"
    fi

    set +e

    while [ $retries -gt 0 ]; do
        temp=$(cat $thermal_zone/temp 2>&1)
        if [ $? -eq 0 ]; then
            break;
        fi
        retries=$(($retries-1))
    done

    set -e

    if [ ! $(echo $temp | grep "^[-0-9]*$") ]; then
        echo "Failed to read temp: $temp" >&2
        exit 1
    fi

    echo $temp
}

bat_get_clk_rate()
{
    cat /sys/kernel/debug/clk/$1/clk_rate
}

if [[ -z `which ip 2>/dev/null` ]]; then
    pr_debug "Automatically added /sbin to PATH"
    export PATH="$PATH:/sbin"
fi
if [[ -z `which chroot 2>/dev/null` ]]; then
    pr_debug "Automatically added /usr/sbin to PATH"
    export PATH="$PATH:/usr/sbin"
fi

machine=$(cat /sys/devices/soc0/machine)
soc_id=$(cat /sys/devices/soc0/soc_id)
test_script=$(basename $0)

if [[ "${todo[$machine]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${machine}, not yet implemented"
    exit $BAT_EXITCODE_TODO
elif [[ "${todo[$soc_id]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${soc_id}, not yet implemented"
    exit $BAT_EXITCODE_TODO
fi

if [[ "${skip[$machine]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${machine}"
    exit $BAT_EXITCODE_SKIP
elif [[ "${skip[$soc_id]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${soc_id}"
    exit $BAT_EXITCODE_SKIP
fi
