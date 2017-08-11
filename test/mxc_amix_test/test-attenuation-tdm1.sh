#!/bin/bash

cd $(dirname $0)
script_name=$(basename $0)
sample_name=${script_name%.*}

if [ "$#" != "2" ]; then
	echo "Please provide two WAV files as parameters."
	exit -1
fi

if [ -r amixenv.sh ]
then
	source amixenv.sh
else
	echo "amixenv.sh is missing!"
	exit -1
fi

procs=(
	"arecord -D hw:${devices[0]} -f S32_LE -c 8 -t wav $sample_name.wav"
	"aplay -D hw:${devices[0]} $1"
	"aplay -D hw:${devices[1]} $2"
)
pids=()

echo " =============================="
echo " AMIX Test Play on both hw:${devices[0]} and hw:${devices[1]}"
echo " =============================="
echo "    Play $1 on hw:${devices[0]}"
echo "    Play $2 on hw:${devices[1]}"
echo "    Record $sample_name.wav on hw:${devices[0]}"
echo " =============================="

for i in "${procs[@]}"
do
	cmd="$i"
	$cmd &
	pids+=($!)
done

#####################################
# Let the play and record start properly
#####################################
sleep 5

#####################################
# Configure attenuation parametes
#####################################

#####################################
# Attenuation Initial value
#####################################
# This is an 18 bit value and taken as fraction always. Example:
# 20000h = 0.5
# 30000h = 0.75
# ...
# 3FFFFh = 0.999996185 (default)
#####################################
amixer -c $card cset name='TDM1 Attenuation Initial Value' 100%

#####################################
# Attenuation Step Divider
# ###################################
# This field denotes the gap, in number of frames, between two
# consecutive steps of calculation of new attenuation value.
# Zero means calculation happens in every frame.
#####################################
amixer -c $card cset name='TDM1 Attenuation Step Divider' 10%

#####################################
# Attenuation Step Target Value
#####################################
# This field denotes the number of times the new attenuation
# value will be calculated for one direction. This is an unsigned
# integer value. Default is set to 16
#####################################
amixer -c $card cset name='TDM1 Attenuation Step Target' 64

#####################################
# Attenuation Step Up Factor, set to 51%
#####################################
# This is an 18 bit value and taken as fraction always. For same attenuation
# transition profile on both direction (downward and upward audio volume) the
# value of step up factor should be kept greater than 0.5 to 1.0
# (i.e > 20000h to 3FFFFh). The default value is kept to keep same transition
# profile with down factor. (the relation for same transition profile:
# step_up_factor * step_dn_factor = 0.5 )
#
# Example:
# 20000h = 0.5
# 30000h = 0.75
# ...
# 2AAAAh = 0.666664124 (default)
# ...
# 3FFFFh = 0.999996185
#####################################
amixer -c $card cset name='TDM1 Attenuation Step Up Factor' 51%

#####################################
# Attenuation Step Down Factor, set to 98%
#####################################
# This is an 18 bit value and taken as fraction always. For same attenuation
# transition profile on both direction (downward and upward audio volume) the
# value of step up factor should be kept greater than 0.5 to 1.0
# (i.e > 20000h to 3FFFFh). The default value is kept to keep same transition
# profile with up factor. (the relation for same transition profile:
# step_up_factor * step_dn_factor = 0.5 )
#
# Example:
# 20000h = 0.5
# 30000h = 0.75 (default)
# ...
# 3FFFFh = 0.999996185
#####################################
amixer -c $card cset name='TDM1 Attenuation Step Down Factor' 98%

####################################
# Test starts here
####################################
sleep 5
amixer -c $card cset name='TDM1 Attenuation Direction' Downward
amixer -c $card cset name='TDM1 Attenuation' Enabled
sleep 30
amixer -c $card cset name='TDM1 Attenuation Direction' Upward
sleep 30
amixer -c $card cset name='TDM1 Attenuation' Disabled
amixer -c $card cset name='Output Source' TDM1

####################################
for pid in "${pids[@]}"
do
	wait $pid
done
echo " =============================="
echo " AMIX Test finished, please check $sample_name.wav"
exit 0

