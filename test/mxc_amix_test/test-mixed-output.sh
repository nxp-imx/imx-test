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

for pid in "${pids[@]}"
do
	wait $pid
done

echo " =============================="
echo " AMIX Test finished, please check $sample_name.wav"
exit 0

