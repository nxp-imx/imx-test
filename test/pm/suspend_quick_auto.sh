#!/bin/sh
# This will only run the quickhit tests.

# standardization of Unit Test
source /unit_tests/test-utils.sh

display_usage() {
        echo -e "This script does quick or custom suspend/resume operation"
        echo -e "Without arguments script trigger 20000 cycles of 2 seconds suspending each iteration"
        echo -e "Usage:"
        echo -e "./suspend_quick_auto.sh [number of cycles] [suspend duration in seconds]"
        }

print_name

function do_wakeup {
    i=0;
        while [ "$i" -lt $1 ];
        do
            /unit_tests/SRTC/rtcwakeup.out -d rtc0 -m mem -s $2;
            i=`expr $i + 1`;
            echo "==============================="
            echo  suspend $i times
            echo "==============================="
        done
}


if [ $# -eq 0 ]
then
    echo "Default mode: 20000 cycles of 2 seconds suspending"
    do_wakeup 20000 2
elif [ ! -z "$1" ] && [ ! -z "$2" ]
then
    echo "Custom mode: $1 cycles of $2 seconds suspending"
    do_wakeup $1 $2
else
    display_usage
fi

print_result
