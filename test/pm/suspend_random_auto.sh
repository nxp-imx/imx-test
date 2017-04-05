#!/bin/sh

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

i=0;
t=2;
d=5;
r=0;
while [ "$i" -lt 20000 ];
do
	/unit_tests/SRTC/rtcwakeup.out -d rtc0 -m mem -s $t;
	sleep $d;
	echo =============================
	echo    suspend $i times;
	echo =============================
	i=`expr $i + 1`;
	r=`date +%s`;
	t=`expr $r % 10 + 2`;
	d=`expr $r % 20 + 1`;
	echo wakeup $t seconds, sleep $d seconds;
done
print_result
