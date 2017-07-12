#! /bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

platform=`cat /sys/devices/soc0/soc_id`
case $platform in
i.MX6U*|i.MX6SX)
    echo "Platform $platform shouldn't have a vddpu"
    have_vddpu=0
    ;;
i.MX6Q*|i.MX6D*|i.MX6SL)
    echo "Platform $platform should have vddpu"
    have_vddpu=1
    ;;
*)
    echo "Non-applicable platform $platform, skip"
    exit $BAT_EXITCODE_SKIP
    ;;
esac

# read a firmware property file (binary) and return an integer
of_read_int() {
    echo $((0x`od -An -tx1 "$1"|tr -d ' '`))
}

ldo_bypass_path=`find /sys/firmware/devicetree -name fsl,ldo-bypass`
if [[ -z $ldo_bypass_path ]]; then
    echo "did not find fsl,ldo-bypass in devicetree, assume 0"
    ldo_bypass=0
else
    ldo_bypass=`of_read_int $ldo_bypass_path`
    echo "devicetree fsl,ldo-bypass property has value $ldo_bypass"
fi

# Read bypass regulator state from HW:
anatop_read_reg_core() {
    if [[ -z $ANATOP_BASE_ADDR ]]; then
        ANATOP_BASE_ADDR=`basename /sys/bus/platform/devices/*.anatop`
        ANATOP_BASE_ADDR=0x${ANATOP_BASE_ADDR%%.*}
    fi
    bat_memtool_addr_value -32 $(printf '0x%x' $(($ANATOP_BASE_ADDR + 0x140))) 1
}

anatop_reg_core=`anatop_read_reg_core`
if [[ -z $anatop_reg_core ]]; then
    echo "Failed to read anatop REG_CORE"
    exit 1
fi
anatop_reg_core_arm=$((anatop_reg_core & 0x1F))
anatop_reg_core_soc=$((anatop_reg_core >> 18 & 0x1F))
if [[ $ldo_bypass == 1 ]]; then
    bat_assert_equal $((0x1F)) $anatop_reg_core_arm "LDO_ARM must be in bypass"
    bat_assert_equal $((0x1F)) $anatop_reg_core_soc "LDO_SOC must be in bypass"
fi

have_vddarmsoc=1
if kernel_is_version 4.1 && [[ $ldo_bypass == 1 ]]; then
    echo "HACK: On imx 4.1 the arm/soc in bypass mode anatop regulators do not probe"
    have_vddarmsoc=0
fi

# Look for the regulators:
if [[ $have_vddarmsoc -ne 0 ]]; then
    sys_vddarm=`ls -d /sys/bus/platform/drivers/anatop*/*regulator-vddcore*/regulator/regulator.*`
    sys_vddsoc=`ls -d /sys/bus/platform/drivers/anatop*/*regulator-vddsoc*/regulator/regulator.*`

    if [[ ! -d $sys_vddarm ]]; then
        echo "missing vddarm?"
        exit 1
    fi

    if [[ ! -d $sys_vddsoc ]]; then
        echo "missing vddsoc?"
        exit 1
    fi
fi

if [[ $have_vddpu -ne 0 ]]; then
    sys_vddpu=`ls -d /sys/bus/platform/drivers/anatop*/*regulator-vddpu*/regulator/regulator.*`
    if [[ ! -d $sys_vddpu ]]; then
        echo "missing vddpu?"
        exit 1
    fi
fi
echo "Found the LDO regulators"

if [[ $have_vddarmsoc -ne 0 ]]; then
    bypass_vddarm=`cat $sys_vddarm/bypass`
    bypass_vddsoc=`cat $sys_vddsoc/bypass`
fi
if [[ $have_vddpu -ne 0 ]]; then
    bypass_vddpu=`cat $sys_vddpu/bypass`
fi

if [[ $ldo_bypass == 0 ]]; then
    expected_bypass_state=disabled
else
    expected_bypass_state=enabled
fi
echo "ldo bypass in devicetree seems to be $expected_bypass_state"

if [[ $have_vddarmsoc -ne 0 ]]; then
    if [[ $bypass_vddsoc != $expected_bypass_state ]]; then
        echo "vddsoc bypass state should be $expected_bypass_state!"
        exit 1
    fi
    if [[ $bypass_vddarm != $expected_bypass_state ]]; then
        echo "vddarm bypass state should be $expected_bypass_state!"
        exit 1
    fi
fi
if [[ $have_vddpu -ne 0 ]]; then
    if [[ $bypass_vddpu != $expected_bypass_state ]]; then
        echo "vddpu bypass state should be $expected_bypass_state!"
        exit 1
    fi
fi

# Find the supply for a regulator
regulator_find_supply() {
    local target target_vin_supply_phandle supply supply_phandle

    # This is surprisingly difficult.
    # We iterate all supplies and check the phandle to do this.
    target=`readlink -f "$1"`
    target_vin_supply_phandle=`of_read_int "$target/of_node/vin-supply"`
    for supply in /sys/class/regulator/*; do
        supply_phandle=`of_read_int "$supply/of_node/phandle" 2>&1`
        if [[ $supply_phandle == $target_vin_supply_phandle ]]; then
            echo $supply
        fi
    done
}

regulator_assert_supply()
{
    local supply
    supply=`regulator_find_supply $1`
    if [[ -z $supply ]]; then
        echo "regulator `cat $1/name` has no supply?"
        exit 1
    fi
    echo "regulator `cat $1/name` supply is `cat $supply/name`"
}

# Check supplies
if [[ $ldo_bypass == 1 ]] && ! kernel_is_version 4.1; then
    if [[ $have_vddarmsoc -ne 0 ]]; then
        regulator_assert_supply "$sys_vddarm"
        regulator_assert_supply "$sys_vddsoc"
    fi
    if [[ $have_vddpu -ne 0 ]]; then
        regulator_assert_supply "$sys_vddpu"
    fi
fi

echo "Individual LDO regulators seem to be in the correct state"
