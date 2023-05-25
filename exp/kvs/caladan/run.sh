#!/bin/bash

source ../../shared.sh

CLT_IDX=3
SRV_IDX=4
#CLT_IDX=1
#SRV_IDX=2

DIR=`pwd`

mops=( 1 2 3 4 5 6 7 8 9 10 11 12 12.2 12.4 12.6 12.8 )

make -j
distribute server $SRV_IDX

for mop in ${mops[@]}
do
    sleep 5
    start_iokerneld $CLT_IDX
    start_iokerneld $SRV_IDX
    sleep 5
    run_program server $SRV_IDX $DIR/conf/server1 &
    sleep 5
    run_cmd $CLT_IDX "cd $DIR;
                      sed \"s/constexpr double kTargetMops =.*/constexpr double kTargetMops = $mop;/g\" -i client.cpp;
                      make clean; make -j"
    run_program client $CLT_IDX $DIR/conf/client1 1>$DIR/logs/$mop 2>&1
    cleanup
done
