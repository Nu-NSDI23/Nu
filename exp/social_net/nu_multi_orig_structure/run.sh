#!/bin/bash

source ../../../setup.sh
source ../../shared.sh

DISK_DEV=/dev/nvme1n1

LPID=1
CTL_IDX=32
NGINX_SRV_IDX=31
CLT_IDX=30
NGINX_SRV_CALADAN_IP_AND_MASK=18.18.1.254/24
TARGET_MOPS=2

DIR=`pwd`
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/multi_proclets/

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

for num_srvs in `seq 1 29`
do
    cd $SOCIAL_NET_DIR
    sed "s/constexpr static uint32_t kNumEntries.*/constexpr static uint32_t kNumEntries = $num_srvs;/g" \
	-i src/BackEndService.cpp
    sed "s/constexpr static uint32_t kNumEntries.*/constexpr static uint32_t kNumEntries = $num_srvs;/g" \
	-i bench/client.cpp
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $TARGET_MOPS;/g" \
	-i bench/client.cpp
    cd build
    make clean
    make -j
    cd ..

    for srv_idx in `seq 1 $num_srvs`
    do
	run_cmd $srv_idx "mkdir -p `pwd`/build/src"
	distribute build/src/BackEndService $srv_idx
    done

    run_cmd $CLT_IDX "mkdir -p `pwd`/build/bench"
    distribute build/bench/client $CLT_IDX

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
	if [[ $srv_idx -ne $num_srvs ]]
	then
	    start_server build/src/BackEndService $srv_idx $LPID &
	else
	    sleep 5
	    start_main_server build/src/BackEndService $srv_idx $LPID >$DIR/logs/.tmp &
	fi
    done
    ( tail -f -n0 $DIR/logs/.tmp & ) | grep -q "Starting the back-end-service server"
    sleep 5

    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"
    sleep 5

    run_program build/bench/client $CLT_IDX $DIR/conf/client2 1>$DIR/logs/$num_srvs 2>&1

    cleanup
    sleep 5
done

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down_nginx.sh;"
run_cmd $NGINX_SRV_IDX "docker rm -vf $(docker ps -aq)"
run_cmd $NGINX_SRV_IDX "docker volume prune -f"
run_cmd $NGINX_SRV_IDX "sudo ip addr delete $NGINX_SRV_CALADAN_IP_AND_MASK dev $nic_dev"
