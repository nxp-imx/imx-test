#! /bin/bash

set -e
. $(dirname $(readlink -f "${BASH_SOURCE[0]}"))/bat_utils.sh

if ! bat_kconfig_enabled CONFIG_NVMEM_IMX_OCOTP; then
    echo "Missing CONFIG_NVMEM_IMX_OCOTP" &>2
    exit $BAT_EXITCODE_SKIP
fi

check_ocotp_nvmem_file() {
    for f in /sys/bus/nvmem/devices/imx-{,scu-}ocotp0/nvmem; do
        if [[ -f $f ]]; then
            echo $f
            return 0
        fi
    done
    echo "Missing imx ocotp device in /sys/bus/nvmem/devices?"
    exit $BAT_EXITCODE_FAIL
}

IMX_NVMEM_FILE=$(check_ocotp_nvmem_file)

echo "Dump first few fuses from imx-ocotp:"
od -v -tx4 --endian=little "$IMX_NVMEM_FILE" --read-bytes=128

# Check read works until the end
if ! dd if="$IMX_NVMEM_FILE" of=/dev/null bs=16; then
    echo "Failed reading $IMX_NVMEM_FILE"
    exit $BAT_EXITCODE_FAIL
fi

exit $BAT_EXITCODE_PASS
