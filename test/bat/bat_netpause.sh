#! /bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

if bat_running_with_nfsroot; then
    bat_reexec_ramroot $@
fi

bat_net_down
sleep $1
bat_net_restore
