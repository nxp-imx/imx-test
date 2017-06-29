#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

cards_no=$(ls -ld /proc/asound/card* | grep ^d | wc -l)
machine=$(cat /sys/devices/soc0/machine)
sample_wav=/unit_tests/ASRC/audio8k16S.wav
duration=1

# check available audio cards
case $machine in
'Freescale i.MX6 SoloX SDB RevB Board'|\
'Freescale i.MX7 SabreSD Board'\
)
    expected_cards_no=2
    ;;
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
    exit 1
fi

# check aplay on each device of each existing card
while read line; do
    dev=$(echo $line | cut -d ':' -f1 | tr - ,)

    aplay -Dhw:$dev -d $duration $sample_wav

    if [ $? -ne 0 ]; then
        exit $?
    fi
done <  /proc/asound/pcm

exit 0
