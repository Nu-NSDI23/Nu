#!/bin/bash

source shared.sh

USAGE="Usage: $0 [tests_prefix]
"
SERVER_IP="18.18.1.2"
MAIN_SERVER_IP="18.18.1.3"
LPID=1
SKIPPED_TESTS=("test_continuous_migrate")

all_passed=1
tests_prefix=
while (( "$#" )); do
	case "$1" in
		-h|--help) echo "$USAGE" >&2 ; exit 0 ;;
		*) tests_prefix=$1 ; shift ;;
	esac
done

function prepare {
    kill_iokerneld
    kill_controller
    sleep 5
    source setup.sh >/dev/null 2>&1
    sudo sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

function run_main_server {
    sudo stdbuf -o0 sh -c "ulimit -c unlimited; $1 -m -l $LPID -i $MAIN_SERVER_IP"
}

function run_server {
    sudo stdbuf -o0 sh -c "ulimit -c unlimited; $1 -l $LPID -i $SERVER_IP"
}

function run_test {
    BIN="$SHARED_SCRIPT_DIR/bin/$1"

    run_controller 1>/dev/null 2>&1 &
    disown -r
    sleep 3

    run_server $BIN 1>/dev/null 2>&1 &
    disown -r
    sleep 3

    run_main_server $BIN 2>/dev/null | grep -q "Passed"
    ret=$?

    kill_process test_
    kill_controller
    sleep 5

    sudo mv core core.$1 1>/dev/null 2>&1

    return $ret
}

function run_tests {
    TESTS=`ls bin | grep $1`
    for test in $TESTS
    do
	if [[ " ${SKIPPED_TESTS[*]} " =~ " $test " ]]; then
	    continue
	fi
	echo "Running test $test..."
	rerun_iokerneld
	run_test $test
	if [[ $? == 0 ]]; then
            say_passed
	else
            say_failed
            all_passed=0
	fi
    done
}

function cleanup {
    kill_iokerneld
}

function force_cleanup {
    echo -e "\nPlease wait for proper cleanups..."
    kill_process test_
    kill_controller
    cleanup
    exit 1
}

prepare
trap force_cleanup INT

if [[ -z $tests_prefix ]]; then
    run_tests test_
else
    run_tests $tests_prefix
fi

cleanup

if [[ $all_passed -eq 1 ]]; then
    exit 0
else
    exit 1
fi
