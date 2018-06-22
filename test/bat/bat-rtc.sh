#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

function find_irq()
{
    for i in $1; do
        if ! [ -z "$(grep $i /proc/interrupts)" ]; then
           echo $i
           return 0
        fi
    done
    echo "Could not find RTC irq" >&2
    return 1
}

soc=$(cat /sys/devices/soc0/soc_id)
if [[ $soc == 'i.MX7ULP' ]]; then
    irq=$(find_irq imx-mu-rpmsg)
else
    irq=$(find_irq "rtc imx8_mu_isr")
fi

if [[ "$irq" != "rtc" ]]; then
    check="-ge"
else
    check="-eq"
fi

tmp_file=$(mktemp)
cpus=$(cat /proc/cpuinfo | grep processor|wc -l)

function sum_irqs()
{
    s=0
    for i in $(grep $irq /proc/interrupts|tr -s ' ' |cut -f2- -d:|cut -f2-$((1+cpus)) -d' '); do
        s=$((s+i))
    done
    echo $s
}

start_irqs=$(sum_irqs)

ALARM_TIMEOUT=1
$batdir/../SRTC/rtctest.out --no-periodic --alarm-timeout $ALARM_TIMEOUT --uie-count 1 &> $tmp_file &
rtctest_pid=$!
if ! bat_wait_timeout $((ALARM_TIMEOUT + 5)) $rtctest_pid; then
    echo "Timeout out rtctest.out:"
    cat $tmp_file
    # Do not attempt to parse rtctest output on timeout
    exit 1
fi

end_irqs=$(sum_irqs)

expected_irqs=$(tail -n2 $tmp_file | egrep -o '[0-9]*')

rm $tmp_file

if ! [ $[$end_irqs - $start_irqs] $check $expected_irqs ]; then
    echo "irq missmatch: got $[$end_irqs - $start_irqs] , expected $expected_irqs"
    exit 1
fi
