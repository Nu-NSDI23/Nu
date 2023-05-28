#!/bin/bash
source ../../shared.sh

CTL_IDX=32
LPID=1

DIR=`pwd`
PHOENIX_DIR=$DIR/../../../app/phoenix++-1.0/nu
KMEANS_DIR=$PHOENIX_DIR/tests/kmeans/

cp kmeans.cpp $KMEANS_DIR/kmeans.cpp
for num_srvs in `seq 1 31`
do
    cd $KMEANS_DIR
    sed "s/constexpr int kNumWorkerNodes.*/constexpr int kNumWorkerNodes = $num_srvs;/g" \
	-i kmeans.cpp
    pushd $PHOENIX_DIR
    make clean
    make -j
    popd

    start_iokerneld $CTL_IDX
    sleep 5
    start_ctrl $CTL_IDX

    for srv_idx in `seq 1 $num_srvs`
    do
	start_iokerneld $srv_idx
    done
    sleep 5

    for srv_idx in `seq 1 $num_srvs`
    do
	distribute kmeans $srv_idx
	if [[ $srv_idx -ne $num_srvs ]]
	then
	    start_server kmeans $srv_idx $LPID &
	else
	    sleep 5
	    start_main_server kmeans $srv_idx $LPID >$DIR/logs/$num_srvs &
	fi
    done
    ( tail -f -n0 $DIR/logs/$num_srvs & ) | grep -q "iter = 10"

    cleanup
    sleep 5
done

