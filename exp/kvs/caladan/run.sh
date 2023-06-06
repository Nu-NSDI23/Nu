#!/bin/bash

source ../../shared.sh

SRV_IDX=1
CLT_START_IDX=2
NUM_CLTS=2

DIR=`pwd`

mops=( 1 2 3 4 5 6 7 8 9 10 11 12 13 14 )

get_clt_idx() {
    echo `expr $1 + $CLT_START_IDX - 1`
}

make clean
make -j
distribute server $SRV_IDX

for mop in ${mops[@]}
do
    start_iokerneld $SRV_IDX
    for i in `seq 1 $NUM_CLTS`
    do
	start_iokerneld $(get_clt_idx $i)
    done
    sleep 5
    run_program server $SRV_IDX $DIR/conf/server &
    sleep 5

    for i in `seq 1 $NUM_CLTS`
    do
	clt_idx=$(get_clt_idx $i)
	run_cmd $clt_idx "cd $DIR;
                          sed \"s/constexpr double kTargetMops =.*/constexpr double kTargetMops = $mop;/g\" -i client.cpp;
                          make clean; make -j"
	run_program client $clt_idx $DIR/conf/client$i 1>$DIR/logs/$mop.$i 2>&1 &
	client_pids+=" $!"
    done
    wait $client_pids

    cleanup
    sleep 5
done
