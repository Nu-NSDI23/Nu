#!/bin/bash

SOURCE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

source $SOURCE_DIR/shared.sh

function prepare {
    kill_iokerneld
    kill_controller
    sleep 5
    source setup.sh >/dev/null 2>&1
    sudo sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

function cleanup {
    kill_iokerneld
    kill_controller
}

function force_cleanup {
    echo -e "\nPlease wait for proper cleanups..."
    kill_process test_
    # kill_process gdb
    cleanup
    exit 1
}

function start_nu_rt {
    echo "Killing related service..."
    prepare
    echo "Rerun iokerneld"
    rerun_iokerneld 
    echo "Run controller"
    run_controller 1>/dev/null 2>&1 &
    disown -r 
    sleep 3
    echo "Start finished"
}

function shutdown_nu_rt {
    cleanup
}

function setup_gdbinit {
    GDBINIT_FILENAME="$HOME/.gdbinit"
    CMD="handle SIGUSR1 SIGUSR2 nostop noprint" 

    touch $GDBINIT_FILENAME

    if ! grep -Fxq "$CMD" "$GDBINIT_FILENAME"; then
        echo "$CMD" >> "$GDBINIT_FILENAME"
    fi
}

trap force_cleanup INT

setup_gdbinit
