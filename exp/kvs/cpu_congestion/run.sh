#!/bin/bash

source ../../shared.sh

NUM_SRVS=15
NUM_CLTS=$NUM_SRVS
SRV_START_IDX=1
VICTIM_IDX=`expr $NUM_SRVS - 1`
CTL_IDX=`expr $NUM_SRVS + $NUM_CLTS + 1`
LPID=1
KS=26
SPIN_KS=`expr $KS / 2`

DIR=`pwd`

get_srv_idx() {
    echo $1
}

get_clt_idx() {
    echo `expr $1 + $NUM_SRVS`
}

disable_kernel_bg_prezero

start_iokerneld $CTL_IDX
for i in `seq 1 $NUM_SRVS`
do
    start_iokerneld $(get_srv_idx $i)
done

for i in `seq 1 $NUM_CLTS`
do
    start_iokerneld $(get_clt_idx $i)
done
sleep 5

start_ctrl $CTL_IDX
sleep 5

sed "s/constexpr uint32_t kNumProxies.*/constexpr uint32_t kNumProxies = $NUM_SRVS;/g" \
    -i server.cpp
sed "s/constexpr uint32_t kNumProxies.*/constexpr uint32_t kNumProxies = $NUM_SRVS;/g" \
    -i client.cpp
make -j

for i in `seq 1 $NUM_SRVS`
do
    srv_idx=$(get_srv_idx $i)
    distribute server $srv_idx

    if [[ $i -ne $NUM_SRVS ]]
    then
	start_server server $srv_idx $LPID $KS $SPIN_KS >logs/srv_$i &
    else
	sleep 5
	start_main_server server $srv_idx $LPID $KS $SPIN_KS >logs/main &
    fi
done

( tail -f -n0 logs/main & ) | grep -q "finish initing"

cpu_antagonist=$NU_DIR/bin/bench_real_cpu_pressure
distribute $cpu_antagonist $VICTIM_IDX
antagonist_log=$DIR/logs/antagonist
run_program $cpu_antagonist $VICTIM_IDX $DIR/conf/antagonist.conf >$antagonist_log &
antagonist_pid=$!
( tail -f -n0 $antagonist_log & ) | grep -q "waiting for signal"

for i in `seq 1 $NUM_CLTS`
do
    clt_idx=$(get_clt_idx $i)
    distribute client $clt_idx

    run_program client $clt_idx $DIR/conf/client$i >logs/$NUM_SRVS.$i &
    client_pids+=" $!"
done

run_cmd $VICTIM_IDX "sleep 20; sudo pkill -SIGHUP bench"
    
wait $client_pids
scp $(ssh_ip $(get_clt_idx 1)):`pwd`/timeseries $DIR/logs/

enable_kernel_bg_prezero
cleanup
