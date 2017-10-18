#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh
alsatool=$batdir/../Audio/mxc_alsa_hw_params.out

cards_no=$(ls -ld /proc/asound/card* | grep ^d | wc -l)
machine=$(cat /sys/devices/soc0/machine)
duration=1

# check available audio cards
case $machine in
'Freescale i.MX8MQ EVK'|\
'Freescale i.MX6 DualLite/Solo SABRE Automotive Board'|\
'Freescale i.MX6 Quad Plus SABRE Automotive Board'\
)
    expected_cards_no=3
    ;;
'Freescale i.MX8QM ARM2'|\
'Freescale i.MX6 Quad SABRE Automotive Board'|\
'Freescale i.MX6 SoloX SDB RevB Board'|\
'Freescale i.MX6 SoloLite EVK Board'|\
'Freescale i.MX6 DualLite SABRE Smart Device Board'|\
'Freescale i.MX6 Quad SABRE Automotive Board'|\
'Freescale i.MX6 Quad SABRE Smart Device Board'|\
'Freescale i.MX6 Quad Plus SABRE Smart Device Board'|\
'Freescale i.MX7 SabreSD Board'\
)
    expected_cards_no=2
    ;;
'Freescale i.MX8QXP LPDDR4 ARM2'|\
'NXP i.MX7ULP EVK'|\
'Freescale i.MX6 UltraLite 14x14 EVK Board'|\
'Freescale i.MX6 ULL 14x14 EVK Board'|\
'Freescale i.MX6SLL EVK Board'\
)
    expected_cards_no=1
    ;;
*)
    expected_cards_no=0
    echo "Unknown machine '$machine'"
    exit $BAT_EXITCODE_SKIP
    ;;
esac

echo "Available audio cards: $cards_no expected: $expected_cards_no"

if [ $cards_no -ne $expected_cards_no ]; then
    echo "machine: ${machine}"
    echo "cards:"
    echo "$(ls -ld /proc/asound/card* | grep ^d)"
    echo "$(cat /proc/asound/cards)"
    exit 1
fi

echo ""
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
