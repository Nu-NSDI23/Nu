#!/bin/bash

source ../../../setup.sh
source ../../shared.sh

DISK_DEV=/dev/nvme1n1

LPID=1
CTL_IDX=2
NGINX_SRV_IDX=3
BACKEND_SRV_IDX=1 # Don't change; the backend's ip is hard-coded in NGINX"s conf.
CLT_IDX=4
NGINX_SRV_CALADAN_IP_AND_MASK=18.18.1.254/24

DIR=`pwd`
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/single_obj/

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

mops=( 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.82 0.84 0.86 0.88 0.90 0.92 )

for mop in ${mops[@]}
do
    cd $SOCIAL_NET_DIR
    sed "s/constexpr uint32_t kNumEntryObjs.*/constexpr uint32_t kNumEntryObjs = 1;/g" -i src/main.cpp
    sed "s/constexpr static uint32_t kNumEntryObjs.*/constexpr static uint32_t kNumEntryObjs = 1;/g" -i bench/client.cpp
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $mop;/g" -i bench/client.cpp
    cd build
    make clean
    make -j
    cd ..

    run_cmd $BACKEND_SRV_IDX "mkdir -p `pwd`/build/src"
    distribute build/src/main $BACKEND_SRV_IDX
    run_cmd $CLT_IDX "mkdir -p `pwd`/build/bench"
    distribute build/bench/client $CLT_IDX

    start_iokerneld $CTL_IDX
    start_iokerneld $BACKEND_SRV_IDX
    start_iokerneld $CLT_IDX
    sleep 5

    start_ctrl $CTL_IDX
    sleep 5

    start_main_server build/src/main $BACKEND_SRV_IDX $LPID >$DIR/logs/.tmp &
    ( tail -f -n0 $DIR/logs/.tmp & ) | grep -q "Starting the ThriftBackEndServer"
    sleep 5

    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"
    sleep 5

    run_program build/bench/client $CLT_IDX $DIR/conf/client 1>$DIR/logs/$mop 2>&1

    cleanup
    sleep 5
done

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down_nginx.sh;"
run_cmd $NGINX_SRV_IDX "docker rm -vf $(docker ps -aq)"
run_cmd $NGINX_SRV_IDX "docker volume prune -f"
run_cmd $NGINX_SRV_IDX "sudo ip addr delete $NGINX_SRV_CALADAN_IP_AND_MASK dev $nic_dev"
