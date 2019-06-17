#! /bin/bash

# 0 means success
BAT_EXITCODE_PASS=0
BAT_EXITCODE_FAIL=1
BAT_EXITCODE_SKIP=2
BAT_EXITCODE_TODO=3
# All other exitcodes not explicitly mentioned also mean "fail"

# skip not yet implemented features or not yet fixed bugs
# (for unsupported features use the skip array below)
declare -A todo=(\
["i.MX6SLL"]="" \
["i.MX8QXP"]="bat-cpuidle.sh" \
["i.MX8QM"]="" \
["i.MX8MQ"]="" \
)

# skip unsupported features
declare -A skip=(\
["i.MX8QXP"]="bat-gpio-keypad.sh bat-pwm.sh" \
["i.MX8QM"]="bat-gpio-keypad.sh" \
["i.MX8MQ"]="bat-pwm.sh" \
["i.MX8MM"]="bat-pwm.sh" \
["i.MX7ULP"]="bat-thermal-read.sh bat-thermal-trip.sh bat-cpuidle-sleeptime.sh" \
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

# Temporarily unbind one or more devices until bat_unbind_restore_all
bat_unbind_device()
{
    local item dev drv
    for item; do
        pr_debug "unbind $item"

        dev=$(basename "$item")
        drv=$(readlink -f "$item/driver")
        echo $dev > $drv/unbind

        # Prepend so that rebinding is done in order
        BAT_UNBIND_RESTORE=("$drv/$dev" "${BAT_UNBIND_RESTORE[@]}")
    done
}

bat_unbind_device_glob()
{
    bat_unbind_device $(ls -d $@ 2>/dev/null)
}

bat_unbind_restore_all()
{
    local item dev drv
    for item in "${BAT_UNBIND_RESTORE[@]}"; do
        pr_debug "rebind $item"

        dev=$(basename "$item")
        drv=$(dirname "$item")
        echo $dev > $drv/bind
    done
    unset BAT_UNBIND_RESTORE
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
        for fs in proc sys dev sys/kernel/debug; do
            mkdir -p "$RAMROOT/$fs"
            mount -o bind "/$fs" "$RAMROOT/$fs"
        done

        local batdir
        batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))

        # copy
        {
            which bash
            which busybox
            which cpufreq-info
            which cpufreq-set
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
            which sed
            which awk
            echo "$batdir/../memtool"
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

        local bat_connman_status=$(systemctl is-active connman)
        if [ "$bat_connman_status" == active ]; then
            systemctl stop connman
        fi

        local xflag
        if [[ $- == *x* ]]; then
            xflag=-x
        fi

        set +e
        chroot "$RAMROOT" /bin/bash $xflag "/bin/`basename $0`" $@ &> /dev/console
        chroot_exec_status=$?
        pr_debug "cleaning up ramroot"
        umount -R "$RAMROOT"
        rm -rf "$RAMROOT"

        if [ "$bat_connman_status" == active ]; then
            systemctl start connman
        fi

        exit $chroot_exec_status
    fi
}

# Wait for ipv4 device $1 to be registered
bat_net_dev_wait_registered()
{
    while [[ ! -d /sys/class/net/$1 ]]; do
        echo "waiting for net device $1 ..."
        sleep 1
    done
}

# List ipv4 addresses for device $1
bat_net_dev_list_ipaddr()
{
    ip -4 -o addr show dev $1 scope global | awk '{ print $4 }'
}

# Restore ip addresses for device $1
# The list of ipaddr must be passed in $2
bat_net_dev_restore_ipaddr()
{
    local dev=$1 curr_list ipaddr

    curr_list=$(bat_net_dev_list_ipaddr $dev)
    for ipaddr in $2; do
        if echo "$curr_list" | grep -q -v "$ipaddr"; then
            ip addr add $ipaddr dev $dev
        fi
    done
}

# Set network down and save config
bat_net_down()
{
    bat_eth0_status=$(ip link show up | grep eth0 || true)
    bat_eth1_status=$(ip link show up | grep eth1 || true)
    bat_eth0_saved_route=$(ip route show | grep '^default' | grep eth0 || true)
    bat_eth1_saved_route=$(ip route show | grep '^default' | grep eth1 || true)
    bat_eth0_saved_ipaddr=$(bat_net_dev_list_ipaddr eth0)
    bat_eth1_saved_ipaddr=$(bat_net_dev_list_ipaddr eth1)

    if [[ -n $bat_eth0_status ]]; then
        ip link set eth0 down
    fi
    if [[ -n $bat_eth1_status ]]; then
        ip link set eth1 down
    fi

    bat_unbind_device_glob "/sys/bus/platform/drivers/imx_usb/*.usb"
}

# Restore after bat_net_down and clean config
# No effect if called multiple times or without bat_net_down
bat_net_restore()
{
    bat_unbind_restore_all

    if [[ -n $bat_eth0_status ]]; then
        bat_net_dev_wait_registered eth0
        ip link set eth0 up
        unset bat_eth0_status
    fi
    if [[ -n $bat_eth0_saved_ipaddr ]]; then
        bat_net_dev_restore_ipaddr eth0 "$bat_eth0_saved_ipaddr"
        unset bat_eth0_saved_ipaddr
    fi
    if [[ -n $bat_eth0_saved_route ]]; then
        if ! ip route show | grep '^default' | grep eth0; then
            ip route add $bat_eth0_saved_route
        fi
        unset bat_eth0_saved_route
    fi

    if [[ -n $bat_eth1_status ]]; then
        bat_net_dev_wait_registered eth1
        ip link set eth1 up
        unset bat_eth1_status
    fi
    if [[ -n $bat_eth1_saved_ipaddr ]]; then
        bat_net_dev_restore_ipaddr eth1 "$bat_eth1_saved_ipaddr"
        unset bat_eth1_saved_ipaddr
    fi
    if [[ -n $bat_eth1_saved_route ]]; then
        if ! ip route show | grep '^default' | grep eth1; then
            ip route add $bat_eth1_saved_route
        fi
        unset bat_eth1_saved_route
    fi
}

# Prepare for low busfreq other then network down
bat_lowbus_prepare()
{
    # blank fb (state not saved)
    if [[ -f /sys/class/graphics/fb0/blank ]]; then
        echo 1 > /sys/class/graphics/fb0/blank
    fi

    # turn on debug prints in dmesg
    BAT_SAVED_PRINTK=$(cat /proc/sys/kernel/printk)
    echo 8 > /proc/sys/kernel/printk

    # set cpufreq governor to powersave
    if [[ -d /sys/devices/system/cpu/cpu0/cpufreq ]]; then
        BAT_SAVED_CPUFREQ_GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
        cpufreq-set -g powersave
    fi

    bat_unbind_device_glob "/sys/bus/sdio/drivers/brcmfmac/mmc*"
}

# Undo bat_lowbus_prepare and clean saved config
#
# No effect if called multiple times or without bat_lowbus_prepare
bat_lowbus_cleanup()
{
    bat_unbind_restore_all

    if [[ -n $BAT_SAVED_CPUFREQ_GOV ]]; then
        cpufreq-set -g $BAT_SAVED_CPUFREQ_GOV
        unset BAT_SAVED_CPUFREQ_GOV
    fi

    if [[ -n $BAT_SAVED_PRINTK= ]]; then
        echo $BAT_SAVED_PRINTK > /proc/sys/kernel/printk
        BAT_SAVED_PRINTK=
    fi

    if [[ -f /sys/class/graphics/fb0/blank ]]; then
        echo 0 > /sys/class/graphics/fb0/blank
    fi
}

# Wait for busfreq to enter low mode by polling a relevant clk rate
bat_wait_busfreq_low()
{
    local clk_name clk_rate tgt_rate=24000000

    clk_name=$(bat_find_clk ahb ahb_root_clk ahb_div ahb_src)

    for (( iter = 0; iter < 10; ++iter )); do
        bat_read_clk_rate clk_rate $clk_name
        if [[ $clk_rate -le $tgt_rate ]]; then
            echo "wait done: clk $clk_name rate $clk_rate less than $tgt_rate"
            return 0
        else
            echo "waiting... clk $clk_name rate $clk_rate less than $tgt_rate"
        fi
        sleep 1
    done
    echo "timed out waiting to enter low busfreq"
    return 1
}

pr_debug()
{
    echo "$@" >&2
}

kernel_is_version()
{
    version=$(cat /proc/version | sed -s 's/Linux version \([1-9][.][0-9]*[.][0-9]*\).*/\1/')
    if [[ $version == $1.* ]]; then
        return 0
    else
        return 1
    fi
}

# Find memtool
bat_which_memtool()
{
    if [[ -z $BAT_MEMTOOL_EXE ]]; then
        if [[ -x $(which memtool 2>/dev/null) ]]; then
            BAT_MEMTOOL_EXE=$(which memtool)
        else
            BAT_MEMTOOL_EXE="${batdir}/../memtool"
        fi
    fi
    echo -n "$BAT_MEMTOOL_EXE"
}

# Run memtool
bat_memtool() {
    "$(bat_which_memtool)" "$@"
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

# Wait at most $1 for a bunch of pids
bat_wait_timeout()
{
    local result timeout tmptimedout killpid

    if [[ $# -lt 2 ]]; then
        echo "bat_wait_timeout: Needs at least 2 arguments" >&2
        exit -1
    fi

    timeout=$1
    shift

    tmptimedout=`mktemp`
    echo 0 > $tmptimedout

    # Start a killer in background
    (
        sleep $timeout
        echo "kill -9 $@">&2
        echo 1 > $tmptimedout
        kill -9 $@
    ) &
    killpid=$!

    # wait
    wait $@ &> /dev/null || true

    # kill the killer if not already dead
    kill $killpid &> /dev/null || true
    wait $killpid &> /dev/null || true
    read -r result < $tmptimedout
    rm -f $tmptimedout
    return $result
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
        read -r freq < /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_cur_freq
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

bat_has_busfreq()
{
    [[ -d /proc/device-tree/soc/busfreq ||
       -d /proc/device-tree/busfreq ||
       -d /proc/device-tree/interconnect ]]
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

bat_read_clk_rate()
{
    read $1 < /sys/kernel/debug/clk/$2/clk_rate
}

bat_has_clk()
{
    [[ -d /sys/kernel/debug/clk/$1 ]]
}

# Return the first clk in the list which is available
#
# Clock with similar function have different names between chips or
# kernels so find the right one by checking all in a list.
bat_find_clk()
{
    local clk
    for clk; do
        if bat_has_clk $clk; then
            echo -n "$clk"
            return 0
        fi
    done
    echo "None of the following clks found: $@" >&2
    return 1
}

bat_assert_clk_rate()
{
    local clk_name=$1 good_rate=$2 real_rate
    bat_read_clk_rate real_rate $clk_name
    if [[ $real_rate != $good_rate ]]; then
        echo "fail clk $clk_name rate is $real_rate not $good_rate"
        return 1
    else
        echo "pass clk $clk_name rate is $good_rate"
    fi
}

if [[ -z `which ip 2>/dev/null` ]]; then
    pr_debug "Automatically added /sbin to PATH"
    export PATH="$PATH:/sbin"
fi
if [[ -z `which chroot 2>/dev/null` ]]; then
    pr_debug "Automatically added /usr/sbin to PATH"
    export PATH="$PATH:/usr/sbin"
fi

machine=$(cat /sys/devices/soc0/machine||true)
soc_id=$(cat /sys/devices/soc0/soc_id||true)
test_script=$(basename $0)

if [[ -n $machine && "${todo[$machine]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${machine}, not yet implemented"
    exit $BAT_EXITCODE_TODO
elif [[ -n $soc_id && "${todo[$soc_id]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${soc_id}, not yet implemented"
    exit $BAT_EXITCODE_TODO
fi

if [[ -n $machine && "${skip[$machine]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${machine}"
    exit $BAT_EXITCODE_SKIP
elif [[ -n $soc_id && "${skip[$soc_id]}" =~ "$test_script" ]]; then
    echo "Skipping $test_script for ${soc_id}"
    exit $BAT_EXITCODE_SKIP
fi
