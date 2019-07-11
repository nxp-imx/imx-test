#! /bin/bash
#
# This test checks clk rates in low busfreq mode
#

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. "$batdir/bat_utils.sh"

if ! bat_has_busfreq; then
    echo "Missing busfreq node in device-tree, seems not implemented"
    exit "$BAT_EXITCODE_SKIP"
fi

if bat_running_with_nfsroot; then
    bat_reexec_ramroot "$@"
fi


# Dump an union of relevant clk from all imx
bat_dump_busfreq_rates()
{
    local clk_name clk_rate
    for clk_name in \
            ahb ahb_root_clk ahb_div ahb_src \
            axi main_axi main_axi_root_clk main_axi_src \
            ipg ipg_root ipg_root_clk \
            noc noc_div \
            dram dram_root_clk \
            dram_core_clk dram_pll_out dram_alt_root dram_alt dram_apb \
            mmdc mmdc_ch0_axi mmdc_ch1_axi mmdc_p0_fast;
    do
        if bat_has_clk $clk_name; then
            bat_read_clk_rate clk_rate $clk_name
            echo "clk $clk_name rate $clk_rate"
        fi
    done
}

# Every SOC must have these:
ahb=$(bat_find_clk ahb ahb_root_clk ahb_div ahb_src)
ipg=$(bat_find_clk ipg ipg_root ipg_root_clk)

# Print rates once before entering low busfreq:
bat_dump_busfreq_rates

# Enter low busfreq
cleanup()
{
    bat_net_restore
    bat_lowbus_cleanup
}
trap cleanup EXIT
bat_lowbus_prepare
bat_net_down

if ! bat_wait_busfreq_low; then
    echo "failed to check busfreq low rate"
    bat_dump_busfreq_rates
    exit $BAT_EXITCODE_FAIL
fi

# Print rates once after entering low busfreq:
bat_dump_busfreq_rates

bat_assert_busfreq_low()
{
    fail=0
    local soc_id=$(cat /sys/devices/soc0/soc_id||true)
    case $soc_id in
    i.MX8MQ)
        bat_assert_clk_rate $ahb 22222223 || let ++fail
        bat_assert_clk_rate $ipg 11111112 || let ++fail
        noc=$(bat_find_clk noc noc_div)
        bat_assert_clk_rate $noc 100000000 || let ++fail
        axi=$(bat_find_clk main_axi main_axi_div)
        bat_assert_clk_rate $axi 25000000 || let ++fail
        ;;
    i.MX8MM)
        bat_assert_clk_rate $ahb 22222223 || let ++fail
        bat_assert_clk_rate $ipg 11111112 || let ++fail
        noc=$(bat_find_clk noc noc_div)
        bat_assert_clk_rate $noc 150000000 || let ++fail
        axi=$(bat_find_clk main_axi main_axi_div)
        bat_assert_clk_rate $axi 24000000 || let ++fail
        bat_assert_clk_rate dram_apb 20000000 || let ++fail
        ;;
    i.MX7D*)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate main_axi_root_clk 24000000 || let ++fail
        bat_assert_clk_rate dram_root_clk 24000000 || let ++fail
        ;;
    i.MX6SLL)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate axi 24000000 || let ++fail
        bat_assert_clk_rate mmdc_p0_fast 24000000 || let ++fail
        ;;
    i.MX6SL)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate mmdc 24000000 || let ++fail
        ;;
    i.MX6SX)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate mmdc_p0_fast 24000000 || let ++fail
        ;;
    i.MX6UL*)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate axi 24000000 || let ++fail
        bat_assert_clk_rate mmdc_p0_fast 24000000 || let ++fail
        ;;
    i.MX6Q*|i.MX6D*|i.MX6S*)
        bat_assert_clk_rate $ahb 24000000 || let ++fail
        bat_assert_clk_rate $ipg 12000000 || let ++fail
        bat_assert_clk_rate axi 24000000 || let ++fail
        bat_assert_clk_rate mmdc_ch0_axi 24000000 || let ++fail
        ;;
    *)
        echo "No detailed busfreq freq check for $soc_id"
        bat_dump_busfreq_rates
        exit $BAT_EXITCODE_SKIP
        ;;
    esac
    return $fail
}

if bat_assert_busfreq_low; then
    echo "All clk rates as expected during low busfreq"
    exit $BAT_EXITCODE_SUCCESS
else
    echo "Unexpected clk rates during low busfreq"
    exit $BAT_EXITCODE_FAIL
fi
