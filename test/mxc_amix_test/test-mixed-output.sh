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

####################################
for pid in "${pids[@]}"
do
	wait $pid
done

kill -3 $arecpid

echo " =============================="
echo " AMIX Test finished, please check $sample_name.wav"
exit 0

