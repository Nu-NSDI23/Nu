#!/bin/bash

source ../../../shared.sh

CTL_IDX=32
SRV_IDX=1
LPID=1

DIR=`pwd`
PHOENIX_DIR=$DIR/../../../../app/phoenix++-1.0/nu
KMEANS_DIR=$PHOENIX_DIR/tests/kmeans/

cp kmeans_global.cpp $KMEANS_DIR/kmeans.cpp
for num_threads in `seq 1 46`
do
    cd $KMEANS_DIR
    sed "s/constexpr int kNumWorkerThreads.*/constexpr int kNumWorkerThreads = $num_threads;/g" \
	-i kmeans.cpp
    pushd $PHOENIX_DIR
    make clean
    make -j
    popd

    distribute kmeans $SRV_IDX

    start_iokerneld $CTL_IDX
    start_iokerneld $SRV_IDX
    sleep 5

    start_ctrl $CTL_IDX
    sleep 5

    start_main_server kmeans $SRV_IDX $LPID 1>$DIR/logs/$num_threads 2>&1 &
    ( tail -f -n0 $DIR/logs/$num_threads & ) | grep -q "iter = 10"

    cleanup
    sleep 5
done
