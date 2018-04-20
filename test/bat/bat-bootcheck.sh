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

if dmesg|grep -B5 -A20 "WARNING: "; then
    echo "Found WARN during boot" >&2
    exitcode=$BAT_EXITCODE_FAIL
fi

# This means bad DTB:
# OF: /soc/aips-bus@02000000/vpu_fsl@02040000: could not get #power-domain-cells for /soc/aips-bus@02000000/gpc@020dc000
if dmesg|grep -U2 "OF: .* could not get .* for"; then
    echo "Found OF iteration error" >&2
    exitcode=$BAT_EXITCODE_FAIL
fi

if dmesg|grep -U2 "genpd_xlate_onecell: invalid domain index"; then
    echo "Found power domain xlate failure" >&2
    exitcode=$BAT_EXITCODE_FAIL
fi

exit $exitcode
