#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))

tmp_file=$(mktemp)
cpus=$(cat /proc/cpuinfo | grep processor|wc -l)

function sum_irqs()
{
    s=0
    for i in $(grep rtc /proc/interrupts|tr -s ' ' |cut -f2- -d:|cut -f2-$((1+cpus)) -d' '); do
        s=$((s+i))
    done
    echo $s
}

start_irqs=$(sum_irqs)
$batdir/../SRTC/rtctest.out --no-periodic --alarm-timeout 1 --uie-count 1 2>&1 | tee $tmp_file
end_irqs=$(sum_irqs)

expected_irqs=$(tail -n2 $tmp_file | egrep -o '[0-9]*')

rm $tmp_file

if ! [ $[$end_irqs - $start_irqs] -eq $expected_irqs ]; then
    echo "irq missmatch: got $[$end_irqs - $start_irqs] , expected $expected_irqs"
    exit 1
fi
