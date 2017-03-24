#!/bin/sh

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

num=`arecord -l |grep -i "si476" |awk '{ print $2 }'|sed 's/://'`

arecord -D hw:$num,0 -f dat | aplay -D hw:0,0 -f dat &
pid=$!
./mxc_tuner_test.out
echo "killing pid = $pid"
kill $pid

print_result
