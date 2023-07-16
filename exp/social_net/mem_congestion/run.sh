#!/bin/bash

source ../../shared.sh

DISK_DEV=/dev/nvme1n1

LPID=1
CTL_IDX=32
NGINX_SRV_IDX=31
NGINX_SRV_CALADAN_IP_AND_MASK=18.18.1.254/24
CLT_START_IDX=26
CLT_END_IDX=30 #inclusive
NUM_SRVS=25
VICTIM_IDX=$NUM_SRVS
MOPS=4.167

DIR=`pwd`
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/single_proclet/

cp client.cpp $SOCIAL_NET_DIR/bench
cd $SOCIAL_NET_DIR
./build.sh

run_cmd $NGINX_SRV_IDX "sudo apt-get update; sudo apt-get install -y python3-pip; pip3 install aiohttp"
run_cmd $NGINX_SRV_IDX "sudo service docker stop;
                        echo N | sudo mkfs.ext4 $DISK_DEV;
                        sudo umount /mnt;
                        sudo mount $DISK_DEV /mnt;
                        sudo mkdir /mnt/docker;
                        sudo mount --rbind /mnt/docker /var/lib/docker;
                        sudo rm -rf /var/lib/docker/*;
                        sudo service docker start;"
run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./install_docker.sh"
run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down_nginx.sh; ./up_nginx.sh"
run_cmd $NGINX_SRV_IDX "sudo ip addr add $NGINX_SRV_CALADAN_IP_AND_MASK dev $nic_dev"

cd $SOCIAL_NET_DIR
sed "s/constexpr uint32_t kNumEntries.*/constexpr uint32_t kNumEntries = $NUM_SRVS;/g" -i src/main.cpp
sed "s/constexpr static uint32_t kNumEntries.*/constexpr static uint32_t kNumEntries = $NUM_SRVS;/g" \
    -i bench/client.cpp
sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $MOPS;/g" \
    -i bench/client.cpp
cd build
make clean
make -j
cd .. 

for srv_idx in `seq 1 $NUM_SRVS`
do
    run_cmd $srv_idx "mkdir -p `pwd`/build/src"
    distribute build/src/main $srv_idx
done

for clt_idx in `seq $CLT_START_IDX $CLT_END_IDX`
do
    run_cmd $clt_idx "mkdir -p `pwd`/build/bench"
    distribute build/bench/client $clt_idx
done

start_iokerneld $CTL_IDX
for srv_idx in `seq 1 $NUM_SRVS`
do
    start_iokerneld $srv_idx
done
for clt_idx in `seq $CLT_START_IDX $CLT_END_IDX`
do
    start_iokerneld $clt_idx
done
sleep 5

start_ctrl $CTL_IDX
sleep 5

for srv_idx in `seq 1 $NUM_SRVS`
do
    if [[ $srv_idx -ne $NUM_SRVS ]]      
    then
	start_server build/src/main $srv_idx $LPID >$DIR/logs/srv_$srv_idx &
    else       
	sleep 5
	start_main_server build/src/main $srv_idx $LPID >$DIR/logs/main &
    fi
done
( tail -f -n0 $DIR/logs/main & ) | grep -q "Starting the ThriftBackEndServer"
sleep 5

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"
sleep 5

mem_antagonist=$NU_DIR/bin/bench_real_mem_pressure
distribute $mem_antagonist $VICTIM_IDX
antagonist_log=$DIR/logs/antagonist
run_program $mem_antagonist $VICTIM_IDX $DIR/conf/antagonist.conf >$antagonist_log &
antagonist_pid=$!
( tail -f -n0 $antagonist_log & ) | grep -q "waiting for signal"

client_pids=
for clt_idx in `seq $CLT_START_IDX $CLT_END_IDX`
do
    conf=$DIR/conf/client`expr $clt_idx - $CLT_START_IDX + 1`
    run_program build/bench/client $clt_idx $conf 1>$DIR/logs/clt_$clt_idx 2>&1 &
    client_pids+=" $!"
done

run_cmd $VICTIM_IDX "sleep 15; sudo pkill -SIGHUP bench"

wait $client_pids
scp $(ssh_ip $CLT_START_IDX):`pwd`/timeseries $DIR/logs/

run_cmd $VICTIM_IDX "sudo pkill -SIGHUP bench"
wait $antagonist_pid
scp $(ssh_ip $VICTIM_IDX):`pwd`/*traces $DIR/logs/

cleanup
sleep 5

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down_nginx.sh;"
run_cmd $NGINX_SRV_IDX "docker rm -vf $(docker ps -aq)"
run_cmd $NGINX_SRV_IDX "docker volume prune -f"
run_cmd $NGINX_SRV_IDX "sudo ip addr delete $NGINX_SRV_CALADAN_IP_AND_MASK dev $nic_dev"
