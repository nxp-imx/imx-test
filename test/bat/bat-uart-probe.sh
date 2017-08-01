#!/bin/bash

#
# Check that the expected uart ports are successfully registered
# This is mostly board-specific
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

machine=`cat /sys/devices/soc0/machine`
case $machine in
'Freescale i.MX6 UltraLite 14x14 EVK Board'|\
'Freescale i.MX6 ULL 14x14 EVK Board')
    uart_ports="ttymxc0 ttymxc1"
    ;;
"Freescale i.MX6 SoloX SDB RevB Board")
    uart_ports="ttymxc0 ttymxc4"
    ;;
'Freescale i.MX7 SabreSD Board'|\
'Freescale i.MX7D SabreSD Board')
    uart_ports="ttymxc0 ttymxc4 ttymxc5"
    ;;
'Freescale i.MX6 Quad SABRE Automotive Board'|\
'Freescale i.MX6 Quad Plus SABRE Automotive Board'|\
'Freescale i.MX6 DualLite/Solo SABRE Automotive Board')
    uart_ports="ttymxc2 ttymxc3"
    ;;
'NXP i.MX7ULP EVK'|\
'Freescale i.MX8QM ARM2')
    uart_ports="ttyLP0"
    ;;
*)
    echo "Unknown machine $machine, skip"
    exit $BAT_EXITCODE_SKIP
    ;;
esac

# Check that the uart dev nodes exist
echo "Test: check if $machine has uart ports $uart_ports"
anyfail=0
for port in $uart_ports; do
    if ! [ -c /dev/${port} ]; then
        echo "No device /dev/${port}"
        anyfail=1
    else
        echo "Found device /dev/${port}"
    fi
done

exit $anyfail
