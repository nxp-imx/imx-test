#!/bin/bash

source /unit_tests/test-utils.sh

print_result

#
# Exit status is 0 for PASS, nonzero for FAIL
#
STATUS=0

uart_list=($(ls /dev/ttymxc*))

for i in "${uart_list[@]}"; do
	check_devnode "${i}"
done

for i in "${uart_list[@]}"; do
	run_testcase "./mxc_uart_test.out ${i}"
done

print_status
exit $STATUS

