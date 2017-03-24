#!/bin/bash

#keypad test is evtest

# standardization of Unit Test
source /unit_tests/test-utils.sh

print_name

evtest /dev/input/keyboard0

print_result
