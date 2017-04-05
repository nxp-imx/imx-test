#!/bin/sh
# This will only run the quickhit tests.

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

i=0;
while [ "$i" -lt 20000 ];
do
   /unit_tests/SRTC/rtcwakeup.out -d rtc0 -m mem -s 2;
   i=`expr $i + 1`;
   echo "==============================="
   echo  suspend $i times
   echo "==============================="
done
print_result

