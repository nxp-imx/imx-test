#! /bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

flash_devs=`ls -d /sys/block/mtdblock* 2>/dev/null || true`
if [[ -z $flash_devs ]]; then
    echo "No flash devices found on board"
    exit $BAT_EXITCODE_SKIP;
fi

for flash_dev in $flash_devs; do
    # read 1MB
    flash_blkdev=/dev/`basename $flash_dev`
    echo "reading from $flash_blkdev sysfs path `readlink -f $flash_dev`"
    if dd if=$flash_blkdev of=/dev/null bs=512 count=2048; then
        echo "dd read from $flash_blkdev successful"
        exit 0
    else
        echo "dd read from $flash_blkdev failed exit code $?"
        exit 1
    fi
done
