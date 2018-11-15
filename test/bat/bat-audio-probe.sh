#!/bin/bash

#
# Check that the expected sound cards are successfully registered
#

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

cards_no=$(ls -ld /proc/asound/card* | grep ^d | wc -l)
machine=$(cat /sys/devices/soc0/machine)

# check available audio cards
case $machine in
'Freescale i.MX6 DualLite/Solo SABRE Automotive Board'|\
'Freescale i.MX6 Quad Plus SABRE Automotive Board'|\
'FSL i.MX8MM EVK board'|\
'Freescale i.MX8MQ EVK'\
)
    expected_cards_no=3
    ;;
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
'Freescale i.MX8QM ARM2'|\
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

exit 0
