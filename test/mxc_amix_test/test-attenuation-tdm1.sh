#!/bin/bash

dir_name=$(dirname $0)
script_name=$(basename $0)
sample_name=${script_name%.*}

if [ "$#" != "3" ]; then
	echo "Please provide two WAV files for playback and recording rate as parameters."
	exit -1
fi

if [ -r $dir_name/amixenv.sh ]
then
	source $dir_name/amixenv.sh
else
	echo "$dir_name/amixenv.sh is missing!"
	exit -1
fi

areccmd="arecord -q -D hw:${devices[0]} -f S32_LE -c 2 -r $3 -t wav $sample_name.wav"
procs=(
	"aplay -q -D hw:${devices[0]} $1"
	"aplay -q -D hw:${devices[1]} $2"
)
pids=()

# Use TDM1 as mixer clock
prepare_tdm TDM1

echo " =============================="
echo " AMIX Test Play on both hw:${devices[0]} and hw:${devices[1]}"
echo " =============================="
echo "    Play $1 on hw:${devices[0]}"
echo "    Play $2 on hw:${devices[1]}"
echo "    Record $sample_name.wav on hw:${devices[0]}"
echo " =============================="

$areccmd &
arecpid=($!)

for i in "${procs[@]}"
do
	cmd="$i"
	$cmd &
	pids+=($!)
done
####################################
# Test starts here
####################################
sleep 0.01
amixer -q -c $card cset name="Output Source" Mixed

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
amixer -q -c $card cset name='TDM1 Attenuation Initial Value' 100%

#####################################
# Attenuation Step Divider
# ###################################
# This field denotes the gap, in number of frames, between two
# consecutive steps of calculation of new attenuation value.
# Zero means calculation happens in every frame.
#####################################
amixer -q -c $card cset name='TDM1 Attenuation Step Divider' 10%

#####################################
# Attenuation Step Target Value
#####################################
# This field denotes the number of times the new attenuation
# value will be calculated for one direction. This is an unsigned
# integer value. Default is set to 16
#####################################
amixer -q -c $card cset name='TDM1 Attenuation Step Target' 64

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
amixer -q -c $card cset name='TDM1 Attenuation Step Up Factor' 51%

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
amixer -q -c $card cset name='TDM1 Attenuation Step Down Factor' 98%

####################################
# Lets play some attenuation
####################################
sleep 2
amixer -q -c $card cset name='TDM1 Attenuation Direction' Downward
amixer -q -c $card cset name='TDM1 Attenuation' Enabled
sleep 5
amixer -q -c $card cset name='TDM1 Attenuation Direction' Upward
sleep 5
amixer -q -c $card cset name='TDM1 Attenuation' Disabled
amixer -q -c $card cset name='Output Source' TDM1

####################################
for pid in "${pids[@]}"
do
	wait $pid
done

kill -3 $arecpid

echo " =============================="
echo " AMIX Test finished, please check $sample_name.wav"
exit 0

