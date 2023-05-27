#!/bin/bash

source ../../../shared.sh

DIR=`pwd`

cd ../../../../app/phoenix++-1.0/orig/
cp tests/kmeans/kmeans_per_worker.cpp tests/kmeans/kmeans.cpp
make clean
make -j
cd tests/kmeans

#for num_threads in `seq 1 46`
for num_threads in `seq 1 46`
do
    export MR_NUMTHREADS=$num_threads
    ./kmeans 1>$DIR/logs/$num_threads 2>&1 &
    pid=$!
    ( tail -f -n0 $DIR/logs/$num_threads & ) | grep -q "iter = 10"
    kill -9 $pid
    sleep 5
done
