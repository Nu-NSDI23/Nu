#!/bin/bash

source ../../shared.sh

DELAYS=( 100 1000 2000 3000 4000 5000 6000 7000 8000 9000 10000 )
SRV_IDX=1
LPID=1

make clean

for delay in ${DELAYS[@]}
do
    sed "s/\(constexpr uint32_t kDelayNs =\).*/\1 $delay;/g" -i main.cpp
    make

    start_iokerneld $SRV_IDX
    sleep 5

    start_ctrl $SRV_IDX
    sleep 5

    start_main_server main $SRV_IDX $LPID 1>logs/$delay 2>&1 &
    sleep 10

    cleanup
    sleep 5
done
