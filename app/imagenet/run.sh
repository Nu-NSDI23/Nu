#!/bin/bash

cd ../..
source ./shared.sh

all_passed=1
MAIN_SERVER_IP="18.18.1.4"
SERVER1_IP="18.18.1.2"
SERVER2_IP="18.18.1.3"
LPID=1

export OPENCV_LIB_PATH=$ROOT_PATH/app/imagenet/opencv/install/lib

function prepare {
    kill_iokerneld
    kill_controller
    sleep 5
    source ./setup.sh >/dev/null 2>&1
    sudo sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

function run_main_server {
    sudo "LD_LIBRARY_PATH=$OPENCV_LIB_PATH" stdbuf -o0 sh -c "ulimit -c unlimited; $1 -m -l $LPID -i $MAIN_SERVER_IP"
}

function run_server1 {
    sudo "LD_LIBRARY_PATH=$OPENCV_LIB_PATH" stdbuf -o0 sh -c "ulimit -c unlimited; $1 -l $LPID -i $SERVER1_IP"
}

function run_server2 {
    sudo "LD_LIBRARY_PATH=$OPENCV_LIB_PATH" stdbuf -o0 sh -c "ulimit -c unlimited; $1 -l $LPID -i $SERVER2_IP"
}

function run_test {
    BIN="$ROOT_PATH/app/imagenet/$1"

    run_controller 1>/dev/null 2>&1 &
    disown -r
    sleep 3

    run_server1 $BIN 1>/dev/null 2>&1 &
    disown -r
    sleep 3

    run_server2 $BIN 1>/dev/null 2>&1 &
    disown -r
    sleep 3    

    run_main_server $BIN
    ret=0

    kill_process $1
    kill_controller
    sleep 5

    sudo mv core core.$1 1>/dev/null 2>&1

    return $ret
}

function cleanup {
    kill_iokerneld
}

function force_cleanup {
    kill_process $1
    kill_controller
    cleanup
    exit 1
}

trap force_cleanup INT

prepare

rerun_iokerneld
run_test $1

cleanup
