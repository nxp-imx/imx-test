#! /bin/bash

set -e

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

if bat_running_with_nfsroot; then
    bat_reexec_ramroot "$@"
fi

bat_lowbus_prepare
bat_net_down
bat_wait_busfreq_low
set +e
"$@"
set -e
RC=$?
bat_net_restore
bat_lowbus_cleanup

exit $RC
