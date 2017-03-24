#!/bin/bash

source /unit_tests/test-utils.sh

print_name

#
# Exit status is 0 for PASS, nonzero for FAIL
#
STATUS=0

run_mmc_case()
{
	# Generate Test data
	dd if=/dev/urandom of=/home/root/mmc_data bs=512 count=10240

	dd if=/home/root/mmc_data of=/dev/mmcblk0 bs=512 count=10240
	dd if=/dev/mmcblk0 of=/home/root/mmc_data1 bs=512 count=10240

	cmp /home/root/mmc_data1 /home/root/mmc_data

	if [ "$?" = 0 ]; then
		printf "MMC test passes \n\n"
		rm /home/root/mmc_data
		rm /home/root/mmc_data1
	else
		STATUS=1
		printf "MMC test fails \n\n"
	fi
}

# devnode test
check_devnode "/dev/mmcblk0"

if [ "$STATUS" = 0 ]; then
	run_mmc_case
fi

print_status
exit $STATUS
