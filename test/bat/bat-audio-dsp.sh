#!/bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

machine=$(cat /sys/devices/soc0/machine)

# filter for supported platforms
case $machine in
'Freescale i.MX8QXP MEK'|\
'Freescale i.MX8QXP LPDDR4 ARM2'\
)
    ;;
*)
    echo "DSP not supported on '$machine'"
    exit $BAT_EXITCODE_SKIP
    ;;
esac

function check_binaries()
{
    if [ ! -f /lib/firmware/imx/dsp/hifi4.bin ]; then
        exit $BAT_EXITCODE_SKIP
    fi

    if [ ! -f /usr/lib/imx-mm/audio-codec/dsp/lib_dsp_codec_wrap.so ]; then
        exit $BAT_EXITCODE_SKIP
    fi

    if [ ! -f /usr/lib/imx-mm/audio-codec/dsp/lib_dsp_mp3_dec.so ]; then
        exit $BAT_EXITCODE_SKIP
    fi
}

#param: $1 - file type, $2 - input file, $3 - reference file
function do_decode()
{
    XXX_OUT_FILE=$2.out
    dsp_test -f$1 -i$2 -o$XXX_OUT_FILE

    diff $3 $XXX_OUT_FILE &> /dev/null
    res=$?
    rm -f $XXX_OUT_FILE

    if [[ $res == 0 ]]; then
        exit 0
    else
         exit $BAT_EXITCODE_FAIL
    fi
}

# MP3 decode
MP3_IN_FILE=/unit_tests/Audio/sample_22_frames.mp3
MP3_REF_FILE=/unit_tests/Audio/sample_22_frames.mp3.pcm

check_binaries

do_decode 1 $MP3_IN_FILE $MP3_REF_FILE
