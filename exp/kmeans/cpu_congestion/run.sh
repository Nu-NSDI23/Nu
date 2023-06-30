#!/bin/bash

source ../../shared.sh
caladan_use_small_rto # Cloudlab's amd instances are buggy and drop pkts from time to time.
                      # To avoid the performance impact caused by packet losses, we use a small RTO.

NUM_SRVS=29
VICTIM_IDX=$NUM_SRVS
STANDBY_IDX=`expr $VICTIM_IDX + 1`
CLT_IDX=`expr $STANDBY_IDX + 1`
CTL_IDX=`expr $CLT_IDX + 1`
LPID=1
SPIN_KS=23

DIR=`pwd`
PHOENIX_DIR=$DIR/../../../app/phoenix++-1.0/nu
KMEANS_DIR=$PHOENIX_DIR/tests/kmeans/

cp kmeans.cpp $KMEANS_DIR/kmeans.cpp
cd $KMEANS_DIR
sed "s/constexpr int kNumWorkerNodes.*/constexpr int kNumWorkerNodes = $NUM_SRVS;/g" \
    -i kmeans.cpp
pushd $PHOENIX_DIR
make clean
make -j
popd

start_iokerneld $CTL_IDX
start_iokerneld $CLT_IDX
for srv_idx in `seq 1 $NUM_SRVS`
do
    start_iokerneld $srv_idx
done
start_iokerneld $STANDBY_IDX
sleep 5

start_ctrl $CTL_IDX
sleep 5

for srv_idx in `seq 1 $NUM_SRVS`
do
    distribute kmeans $srv_idx
    start_server kmeans $srv_idx $LPID $SPIN_KS &
done
distribute kmeans $STANDBY_IDX

cpu_antagonist=$NU_DIR/bin/bench_real_cpu_pressure
distribute $cpu_antagonist $VICTIM_IDX
run_program $cpu_antagonist $VICTIM_IDX $DIR/antagonist.conf &

sleep 5
distribute kmeans $CLT_IDX
start_main_server_isol kmeans $CLT_IDX $LPID $SPIN_KS >$DIR/logs/$NUM_SRVS &

log=$DIR/logs/$NUM_SRVS
( tail -f -n0 $log & ) | grep -q "iter = 0"
start_server kmeans $STANDBY_IDX $LPID $SPIN_KS &

( tail -f -n0 $log & ) | grep -q "iter = 5"
run_cmd $VICTIM_IDX "sudo pkill -SIGHUP bench"
( tail -f -n0 $log & ) | grep -q "iter = 10"

cleanup
sleep 5

caladan_use_default_rto
