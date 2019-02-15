#! /bin/bash

batdir=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. $batdir/bat_utils.sh

if ! modinfo tcrypt > /dev/null; then
    echo "Missing tcrypt module?"
    exit $BAT_EXITCODE_SKIP
fi

dmesg -c > /dev/null
modprobe tcrypt || TCRYPT_RESULT=$?

fail_tolerated=0
fail_unexpected=0
while read -r line; do
    if [[ $line == *" Failed to load transform "* ]]; then
        fail_tolerated=1
    elif [[ $line == *" failed to allocate transform "* ]]; then
        fail_tolerated=1
    elif [[ $line == *" Failed to load tfm "* ]]; then
        fail_tolerated=1
    elif [[ $line == *"alg: No test for "* ]]; then
        fail_tolerated=1
    elif [[ $line == *"self-tests disabled"* ]]; then
        fail_tolerated=1
    else
        fail_unexpected=1
        echo -E "$line"
    fi
done < <(dmesg | grep alg: )

if [[ $fail_unexpected != 0 ]]; then
    echo "tcrypt reported unexpected failures"
    exit $BAT_EXITCODE_FAIL
fi
if [[ $TCRYPT_RESULT == 0 && $fail_tolerated == 0 ]]; then
    echo "tcrypt passed perfectly"
else
    echo "tcrypt passed other than missing algs/tests"
fi
exit $BAT_EXITCODE_PASS
