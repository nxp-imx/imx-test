#!/bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

if ! dmesg|head -n5|grep -q "Linux version $(uname -r)"; then
    # Maybe dmesg was cleared?
    echo "dmesg does not start with version banner for $(uname -r)" >&2
    exit "$BAT_EXITCODE_SKIP"
fi

exitcode=0

if dmesg|grep -B5 -A20 "Internal error: Oops:"; then
    echo "Found oops during boot" >&2
    exitcode=$BAT_EXITCODE_FAIL
fi

exit $fail
