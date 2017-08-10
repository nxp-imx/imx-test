#! /bin/bash

set -e

print_help() {
    cat >&2 <<EOM
bat.sh - Basic Acceptance Test

== Usage ==
Output tries to be in TAP format (www.testanything.org)

Each individual bat-*.sh script is a single test. Each test is executed and
must exit 0 on succes, 2 on SKIP, 3 on TODO or anything else on error.

== Options ==
    --help: Print this message
EOM
}

# Some tests can bring down the entire system on failure, run them at the end.
BAT_TESTS_LATE=(bat-busfreq.sh bat-suspend.sh)

cd $(dirname $(readlink -f "${BASH_SOURCE[0]}"))
. bat_utils.sh

parse_args() {
    local opts
    opts=$(getopt -n bat.sh -o h -l "help" -- "$@")
    eval set -- "$opts"
    while [ $# -gt 0 ]; do
        case "$1" in
        -h|--help) print_help; exit 0 ;;
        --) ;;
        *) echo "Ignoring unknown argument $1" ;;
        esac
        shift
    done
}

main() {
    parse_args "$@"

    test_scripts=($(
            ls bat-*.sh | grep -v -F -f \
                    <(printf '%s\n' "${BAT_TESTS_LATE[@]}"))
            "${BAT_TESTS_LATE[@]}")

    echo "1..${#test_scripts[@]}"

    count_ok=0
    count_skip=0
    count_todo=0
    count_fail=0

    index=0
    for test_script in "${test_scripts[@]}"; do
        # Newline between tests
        echo

        index=$(($index + 1))
        test_log=/tmp/$test_script.log
        echo "run $index $test_script"

        set +e
        ./$test_script |& tee $test_log
        test_status=$PIPESTATUS
        set -e

        if [[ $test_status == 0 ]]; then
            echo "ok $index $test_script"
            count_ok=$((count_ok + 1))
        elif [[ $test_status == $BAT_EXITCODE_SKIP ]]; then
            echo "ok $index $test_script #SKIP"
            count_skip=$((count_skip + 1))
        elif [[ $test_status == $BAT_EXITCODE_TODO ]]; then
            echo "not ok $index $test_script #TODO"
            count_todo=$((count_todo + 1))
        else
            echo "not ok $index $test_script"
            count_fail=$((count_fail + 1))
        fi
    done

    echo "# bat.sh summary $count_ok ok $count_skip skip $count_todo todo $count_fail fail"

    if [[ $count_fail == 0 ]]; then
        exit 0;
    else
        exit 1;
    fi
}

main "$@"
