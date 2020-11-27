#!/bin/sh

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

numa=`arecord -l |grep -i "si476" |awk '{ print $2 }'|sed 's/://'`
numb=`aplay -l |grep -i "cs42888" |awk 'NR==1 { print $2 }'|sed 's/://'`

arecord -D hw:$numa,0 -f dat | aplay -D hw:$numb,0 -f dat &
pid=$!
./mxc_tuner_test.out
echo "killing pid = $pid"
kill $pid

print_result
