#!/bin/bash

source /unit_tests/test-utils.sh

print_name

if [ ! -e /dev/galcore ]; then
	echo gpu.sh not supported on $(platform)
	exit $STATUS
fi
#
#Save current directory
#
pushd .
#
#run modprobe test
#
#gpu_mod_name=galcore.ko
#modprobe_test $gpu_mod_name
#
#run tests
#
cd /opt/viv_samples/vdk/ && ./tutorial3 -f 100 && cd - &>/dev/null
cd /opt/viv_samples/vdk/ && ./tutorial4_es20 -f 100 && cd - &>/dev/null
cd /opt/viv_samples/tiger/ &&./tiger && cd - &>/dev/null
echo press ESC to escape...
#
#remove gpu modules
#
#rmmod $gpu_mod_name
#restore the directory
#
popd
print_result
