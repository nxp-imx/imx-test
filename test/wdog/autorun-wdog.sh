#!/bin/bash

source /unit_tests/test-utils.sh

print_name

#
# Exit status is 0 for PASS, nonzero for FAIL
#
STATUS=0

check_devnode "/dev/watchdog"

print_status
exit $STATUS
