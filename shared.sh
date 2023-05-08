#!/bin/bash

ROOT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
CALADAN_PATH=$ROOT_PATH/caladan

function say_failed() {
    echo -e "----\e[31mFailed\e[0m"
}

function say_passed() {
    echo -e "----\e[32mPassed\e[0m"
}

function assert_success {
    if [[ $? -ne 0 ]]; then
        say_failed
        exit -1
    fi
}

function kill_process {
    pid=`pgrep $1`
    if [ -n "$pid" ]; then
	{ sudo kill $pid && sudo wait $pid; } 2>/dev/null
    fi
}

function kill_iokerneld {
    kill_process iokerneld
}

function run_iokerneld {
    kill_iokerneld
    sleep 3
    sudo bash -c "$CALADAN_PATH/iokerneld $@ > /dev/null 2>&1 &"
    assert_success
    sleep 5
}

function rerun_iokerneld {
    kill_iokerneld
    run_iokerneld ias
}

function run_controller {
    sudo stdbuf -o0 sh -c "bin/ctrl_main"
}

function kill_controller {
    kill_process ctrl_main
}
