#!/bin/bash

devices=( `aplay -l | grep imxaudmix | \
awk '{
 sub(/card /, "");
 sub(/:/, ",");
 sub(/ imxaudmix \[imx-audmix\], device /, "");
 sub(/:/, "");
 sub(/HiFi-AMIX-FE \(\*\) \[\]/, "");
 print }'` )

if [ ${#devices[@]} -lt 2 ]
then
	echo "The board does not have AMIX support enabled!";
	exit -1;
fi

card=`echo ${devices[0]} | awk '{ sub(/,[0-9]/, ""); print }'`

function prepare_tdm() {
	local tdm=$1
	local pids=()

	case "$tdm" in
	TDM1)	;;
	TDM2)	;;
	*)
		echo "Unknown TDM: $tdm"
		return
	esac

	for dev in "${devices[@]}"; do
		aplay -q -D hw:${dev} -d 2 -c 2 -f S16_LE -r 8000 /dev/zero &
		pids+=($!)
	done

	sleep 0.5
	amixer -q -c $card cset name="Output Source" $tdm

	for pid in "${pids[@]}"; do
		wait $pid
	done
}

