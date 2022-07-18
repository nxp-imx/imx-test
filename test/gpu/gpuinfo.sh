#!/bin/bash

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

STATUS=0
if [ ! -d /sys/kernel/debug/gc ]; then
	echo "gpuinfo.sh not supported on $(platform)"
	exit $STATUS
fi

input=$1
echo "GPU Info"
cat /sys/kernel/debug/gc/info
cat /sys/kernel/debug/gc/meminfo
if [ -e /sys/kernel/debug/gc/allocators/default/lowHighUsage ]; then
	echo "Paged memory Info"
	cat /sys/kernel/debug/gc/allocators/default/lowHighUsage
fi
echo "CMA memory info"
cat /sys/kernel/debug/gc/allocators/cma/cmausage
#echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
time=`cat /sys/kernel/debug/gc/idle`
#echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
if [ x$input != x ]
then
pidlist=`cat /sys/kernel/debug/gc/clients | awk '{if ($1 == '$input') print $1}'`
if [ x$pidlist == x ]
then
pidlist=`cat /sys/kernel/debug/gc/clients | awk '{if ($2~/'$input'/) print $1}'`
fi
if [ x$pidlist == x ]
then
echo "Invalid input: $input"
fi
else
pidlist=`cat /sys/kernel/debug/gc/clients | awk '{if ($1~/^[0-9]/) print $1}'`
fi
for i in $pidlist
do
    echo $i > /sys/kernel/debug/gc/vidmem
    cat /sys/kernel/debug/gc/vidmem
done
echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
sleep 1
time=`cat /sys/kernel/debug/gc/idle | awk '{printf "%.2f", $7/10000000.0}'`
echo "Idle percentage:"$time"%"
echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
print_result
