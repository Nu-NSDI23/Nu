#!/bin/bash

source ../../shared.sh

CTL_IDX=1
SRV_IDX=2
CLT_START_IDX=3
NUM_CLTS=2
LPID=1
KS=26

DIR=`pwd`

mops=( 1 2 3 4 5 6 7 8 9 10 11 12 13 )

get_clt_idx() {
    echo `expr $1 + $CLT_START_IDX - 1`
}

make clean
make -j
distribute server $SRV_IDX

for mop in ${mops[@]}
do
    start_iokerneld $SRV_IDX
    start_iokerneld $CTL_IDX
    for i in `seq 1 $NUM_CLTS`
    do
	start_iokerneld $(get_clt_idx $i)
    done
    sleep 5
    start_ctrl $CTL_IDX
    sleep 5
    start_main_server server $SRV_IDX $LPID $KS $KS >logs/.tmp &
    ( tail -f -n0 logs/.tmp & ) | grep -q "finish initing"
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

