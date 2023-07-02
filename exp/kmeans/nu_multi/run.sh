#!/bin/bash

source ../../shared.sh
caladan_use_small_rto # Cloudlab's amd instances are buggy and drop pkts from time to time.
                      # To avoid the performance impact caused by packet losses, we use a small RTO.

CTL_IDX=32
CLT_IDX=31
LPID=1
KS=23

DIR=`pwd`
PHOENIX_DIR=$DIR/../../../app/phoenix++-1.0/nu
KMEANS_DIR=$PHOENIX_DIR/tests/kmeans/

cp kmeans.cpp $KMEANS_DIR/kmeans.cpp
for num_srvs in `seq 1 30`
do
    cd $KMEANS_DIR
    sed "s/constexpr int kNumWorkerNodes.*/constexpr int kNumWorkerNodes = $num_srvs;/g" \
	-i kmeans.cpp
    pushd $PHOENIX_DIR
    make clean
    make -j
    popd

    start_iokerneld $CTL_IDX
    start_iokerneld $CLT_IDX
    for srv_idx in `seq 1 $num_srvs`
    do
	start_iokerneld $srv_idx
    done
    sleep 5

    start_ctrl $CTL_IDX
    sleep 5

    for srv_idx in `seq 1 $num_srvs`
    do
	distribute kmeans $srv_idx
        start_server kmeans $srv_idx $LPID $KS &
    done

    sleep 5
    distribute kmeans $CLT_IDX
    start_main_server_isol kmeans $CLT_IDX $LPID $KS >$DIR/logs/$num_srvs &
    ( tail -f -n0 $DIR/logs/$num_srvs & ) | grep -q "iter = 10"

    cleanup
    sleep 5
done

caladan_use_default_rto
