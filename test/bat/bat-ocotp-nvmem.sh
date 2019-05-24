#! /bin/bash

set -e
. $(dirname $(readlink -f "${BASH_SOURCE[0]}"))/bat_utils.sh

if ! bat_kconfig_enabled CONFIG_NVMEM_IMX_OCOTP; then
    echo "Missing CONFIG_NVMEM_IMX_OCOTP" &>2
    exit $BAT_EXITCODE_SKIP
fi

IMX_NVMEM_FILE=/sys/bus/nvmem/devices/imx-ocotp0/nvmem

# Check existence:
if [[ ! -f $IMX_NVMEM_FILE ]]; then
    echo "Missing $IMX_NVMEM_FILE?"
    exit $BAT_EXITCODE_FAIL
fi

echo "Dump first few fuses from imx-ocotp:"
od -v -tx4 --endian=little "$IMX_NVMEM_FILE" --read-bytes=128

# Check read works until the end
if ! dd if="$IMX_NVMEM_FILE" of=/dev/null bs=16; then
    echo "Failed reading $IMX_NVMEM_FILE"
    exit $BAT_EXITCODE_FAIL
fi

exit $BAT_EXITCODE_PASS
