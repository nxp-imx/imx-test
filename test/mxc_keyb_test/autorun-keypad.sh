#!/bin/bash

source /unit_tests/test-utils.sh

print_name

#
# Exit status is 0 for PASS, nonzero for FAIL
#
STATUS=0

# devnode test
check_devnode "/dev/input/event0"

print_status
exit $STATUS
