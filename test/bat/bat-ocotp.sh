#! /bin/bash

set -e
. $(dirname $(readlink -f "${BASH_SOURCE[0]}"))/bat_utils.sh

FSL_OTP_DIR=/sys/fsl_otp
if [[ ! -d $FSL_OTP_DIR ]]; then
    echo "Missing $FSL_OTP_DIR"
    if bat_kconfig_enabled CONFIG_FSL_OTP; then
        exit 1
    else
        # Skip on upstream
        echo "Missing CONFIG_FSL_OTP"
        exit $BAT_EXITCODE_SKIP
    fi
fi

case `cat /sys/devices/soc0/soc_id` in
i.MX6SLL|i.MX7ULP)
    echo "Platform $platform does not have ethernet, not testing MAC from OCOTP"
    exit 0
    ;;
esac

# Read OTP Mac Registers
mr0=$(printf %08x $(cat $FSL_OTP_DIR/HW_OCOTP_*MAC*0))
mr1=$(printf %08x $(cat $FSL_OTP_DIR/HW_OCOTP_*MAC*1))
if [[ -f $FSL_OTP_DIR/HW_OCOTP_MAC2 ]]; then
    mr2=$(printf %08x $(cat $FSL_OTP_DIR/HW_OCOTP_MAC2))
fi
mac0=${mr1:4:2}:${mr1:6:2}:${mr0:0:2}:${mr0:2:2}:${mr0:4:2}:${mr0:6:2}
if [[ -n "$mr2" ]]; then
    mac1=${mr2:0:2}:${mr2:2:2}:${mr2:4:2}:${mr2:6:2}:${mr1:0:2}:${mr1:2:2}
fi

if [[ -z $mac1 ]]; then
    echo "From OCOPT MAC should be $mac0"
    re="link/ether $mac0"
else
    echo "From OCOPT MAC should be $mac0 or $mac1"
    re="link/ether \($mac0\|$mac1\)"
fi


# Check if any interface has this MAC
macline=$(ip -o link show | grep "$re")
if [[ -z "$macline" ]]; then
    echo "No interface has mac $mac?"
    exit 1
else
    ifname=($(echo "$macline"|sed -ne 's/^[0-9]\+: \([^:]\+\):.*$/\1/pg'))
    echo "MAC from OCOTP assigned to ${ifname[@]}"
fi

exit 0
