#!/bin/bash

source ../../shared.sh

DELAYS=( 100 1000 2000 3000 4000 5000 6000 7000 8000 9000 10000 )
LPID=1
CLT_IDX=1
SRV_IDX=2

make clean

for delay in ${DELAYS[@]}
do
    sed "s/\(constexpr uint32_t kDelayNs = \).*/\1$delay;/g" -i main.cpp
    make

    distribute main $CLT_IDX
    distribute main $SRV_IDX

    start_iokerneld $CLT_IDX
    start_iokerneld $SRV_IDX
    sleep 5

    start_ctrl $CLT_IDX
    sleep 5

    start_server main $SRV_IDX $LPID &
    sleep 5

    start_main_server main $CLT_IDX $LPID 1>logs/$delay 2>&1 &
    sleep 10
    
    cleanup
    sleep 5
done

