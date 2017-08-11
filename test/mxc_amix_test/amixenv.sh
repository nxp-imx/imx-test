#!/bin/bash

devices=( `aplay -l | grep amixaudiosai | \
awk '{
 sub(/card /, "");
 sub(/:/, ",");
 sub(/ amixaudiosai \[amix-audio-sai\], device /, "");
 sub(/:/, "");
 sub(/HiFi-AMIX-FE \(\*\) \[\]/, "");
 print }'` )

if [ ${#devices[@]} -lt 2 ]
then
	echo "The board does not have AMIX support enabled!";
	exit -1;
fi

card=`echo ${devices[0]} | awk '{ sub(/,[0-9]/, ""); print }'`

