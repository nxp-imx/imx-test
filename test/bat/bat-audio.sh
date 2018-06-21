#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh
alsatool=$batdir/../Audio/mxc_alsa_hw_params.out

cards_no=$(ls -ld /proc/asound/card* | grep ^d | wc -l)
machine=$(cat /sys/devices/soc0/machine)
duration=1

# Check aplay/arecord on each device of each existing card. Run two
# scenarios: with the first supported rate, format, channel and
# last supported rate, format, channel.
# perform all tests and return 0 upon success or last faulty exit code
ecode=0
while read line; do
    dev=$(echo $line | cut -d ':' -f1 | tr - ,)
    echo "Testing hw:$line"

    # skip HDMI and S/PDIF devices since we need additional connectors
    # for them to be attached to DUTs
    if grep -qi hdmi <<< $line; then
        echo "hw:$dev device is HDMI. aplay/arecord test will be skipped"
        continue
    fi
    if grep -qi "S/PDIF" <<< $line; then
        echo "hw:$dev device is S/PDIF. aplay/arecord test will be skipped"
        continue
    fi
    if grep -qi "rpmsg" <<< $line; then
        echo "hw:$dev device is RPMSG. Skip aplay/arecord tests for now"
        continue
    fi
    if grep -qi "micfil" <<< $line; then
        echo "hw:$dev device is MICFIL. Skip arecord tests for now"
        continue
    fi

    # check if the device can perform playback and test it
    if [[ $line =~ "playback" ]]; then
        declare -a rate=($($alsatool hw:$dev p r))
        declare -a format=($($alsatool hw:$dev p f))
        declare -a channel=($($alsatool hw:$dev p c))
        aplay -Dhw:$dev -r ${rate[0]} -f ${format[0]} -c ${channel[0]} -d $duration -t raw /dev/zero
        result=$?
        [ $result -ne 0 ] && ecode=$result
        aplay -Dhw:$dev -r ${rate[${#rate[@]}-1]} -f ${format[${#format[@]}-1]} -c ${channel[${#channel[@]}-1]} -d $duration -t raw /dev/zero
        result=$?
        [ $result -ne 0 ] && ecode=$result
    fi

    # check if the device can perform record and test it
    if [[ $line =~ "capture" ]]; then
        if grep -qi "HiFi-AMIX" <<< $line; then
            echo "hw:$dev device is AMIX. Skip simple capture test"
            continue
        fi
        declare -a rate=($($alsatool hw:$dev c r))
        declare -a format=($($alsatool hw:$dev c f))
        declare -a channel=($($alsatool hw:$dev c c))
        arecord -Dhw:$dev -r ${rate[0]} -f ${format[0]} -c ${channel[0]} -d $duration -t raw /dev/null
        result=$?
        [ $result -ne 0 ] && ecode=$result
        arecord -Dhw:$dev -r ${rate[${#rate[@]}-1]} -f ${format[${#format[@]}-1]} -c ${channel[${#channel[@]}-1]} -d $duration -t raw /dev/null
        result=$?
        [ $result -ne 0 ] && ecode=$result
    fi
    echo ""
done <  /proc/asound/pcm

exit $ecode
