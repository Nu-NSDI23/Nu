#!/bin/bash

source ../../shared.sh

CTL_IDX=1
CLT_IDX=2
SRV_IDX=3
LPID=1

DIR=`pwd`

mops=( 1 2 3 4 5 6 7 8 9 10 11 12 12.2 12.4 12.6 12.8 )

make clean
make -j
distribute server $SRV_IDX

for mop in ${mops[@]}
do
    start_iokerneld $CLT_IDX
    start_iokerneld $SRV_IDX
    start_iokerneld $CTL_IDX
    sleep 5
    start_ctrl $CTL_IDX
    sleep 5
    start_main_server server $SRV_IDX $LPID >logs/.tmp &
    ( tail -f -n0 logs/.tmp & ) | grep -q "finish initing"
    run_cmd $CLT_IDX "cd $DIR;
                      sed \"s/constexpr double kTargetMops =.*/constexpr double kTargetMops = $mop;/g\" -i client.cpp;
                      make clean; make -j"
    run_program client $CLT_IDX $DIR/conf/client 1>$DIR/logs/$mop 2>&1
    cleanup
    sleep 5
done
