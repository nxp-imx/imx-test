#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

duration=60
pdm_convert=$batdir/../Audio/mxc_pdm_test.out
card_no=$(cat /proc/asound/pcm | grep pdm | cut -c 2)
base_args="-device hw:${card_no},0 -seconds ${duration}"
declare -a rates=("8000" "16000" "32000" "48000" "64000")

if [[ ! -z $card_no ]]; then
	for rate in "${rates[@]}"
	do
		$pdm_convert $base_args -rate $rate -output test-$rate-mono.raw
	done
else
	echo "imx pdm hifi device not found"
	exit $BAT_EXITCODE_SKIP
fi

exit 0
