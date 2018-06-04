#! /bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

TMPDIR=`mktemp -d`

count_total=0
count_disabled=0

disabled_parent=

# Examine devices
while read -r item; do
    if [[ -a "$item/status" ]]; then
        read -r status < "$item/status"
    else
        status=''
    fi
    count_total=$((count_total + 1))

    if [[ -n $disabled_parent ]]; then
        if [[ $item == $disabled_parent/* ]]; then
            echo "parent $disabled_parent disables child $item" >&2
            status=disabled
        else
            disabled_parent=
        fi
    fi

    case $status in
    ''|okay)
        echo "$item"
        ;;
    disabled|fail|fail-*) 
        count_disabled=$((count_disabled + 1))
        if [[ -z $disabled_parent ]]; then
            disabled_parent=$item
        fi
        echo "$item" > $TMPDIR/disabled_devices.txt
        ;;
    *)
        echo "Unknown device status '$status' for '$item'" >&2
        exit 1
        ;;
    esac
done < <(find /sys/firmware/devicetree/base/ -name compatible -printf '%h\n' | sort) \
    > $TMPDIR/okay_devices.txt

echo "devicetree contains $count_total devices, $count_disabled disabled" >&2
find /sys/devices -name of_node | xargs readlink -f | sort -u > $TMPDIR/bound_devices.txt

machine=$(cat /sys/devices/soc0/machine)

count_unbound=0
while read -r item; do
    item_compat=$(cat $item/compatible|xargs -0 echo)

    if [[ $item == */ethernet@*/mdio/*phy@1 && $machine == *MX8Q* ]]; then
        echo "accept unbound $item compat $item_compat on '$machine'; maybe baseboard not connected"
        continue
    fi

    if [[ $item == */lpspi@*/* && $machine == *MX8QM* ]]; then
        echo "accept unbound $item compat $item_compat on '$machine'; not yet enabled. TODO?"
        continue
    fi

    case $item in
    /sys/firmware/devicetree/base) ;;

    # Some of these items don't always have device nodes
    # but if they are truly broken then other "real" devices will break
    */clocks/*|*/base/clock-*|*/ccm@*|*/scg*@*|*/interrupt-controller@*|*/gpc@*|*linux,cma|*/cpus/l2-cache*)
        echo "accept unbound $item"
        ;;
    *)
        # coresight is a debug feature we don't enable by default
        if [[ $item_compat == arm,coresight* ]]; then
            echo "accept unbound $item compat $item_compat"
        elif [[ $item_compat == nxp,imx8-pd ]]; then
            echo "accept unbound $item compat $item_compat"
        elif [[ $item_compat == fsl,imx8-wu ]]; then
            echo "accept unbound $item compat $item_compat"
        elif [[ $item_compat == arm,idle-state ]]; then
            echo "accept unbound $item compat $item_compat"
        elif [[ $item_compat == micron* ]] && kernel_is_version 4.1; then
            echo "accept unbound $item compat $item_compat (4.1 does not bound mtd nodes)"
        elif [[ $item_compat == si476x-codec ]] && grep -qi automotive /sys/firmware/devicetree/base/model; then
            echo "accept unbound $item compat $item_compat (not connected on board)"
        else
            count_unbound=$((count_unbound + 1))
            echo "unbound device node $item compatible: $item_compat"
        fi
        ;;
    esac
done < <(comm "$TMPDIR/okay_devices.txt" "$TMPDIR/bound_devices.txt" -2 -3)

if [[ $count_unbound -gt 0 ]]; then
    echo "Found $count_unbound unbound devices"
    exit 1
fi
exit 0
