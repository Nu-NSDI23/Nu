#!/bin/bash

source ../../shared.sh

DISK_DEV=/dev/nvme1n1

LPID=1
CTL_IDX=32
NGINX_SRV_IDX=31
NGINX_SRV_CALADAN_IP_AND_MASK=18.18.1.254/24
CLT_START_IDX=26
CLT_END_IDX=30 #inclusive

DIR=`pwd`
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/single_proclet/

MOPS=( 1.3 1.6 1.9 2.2 2.5 2.8 3.1 3.4 3.7 4 4.3 4.6 4.9 5.2 5.5 5.8 6.1 6.4 6.7 7 7.3 7.6 7.9 8.2 8.5 8.8 9.1 9.4 9.7 10 )

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

for num_srvs in `seq 1 25`
do
    mops=${MOPS[`expr $num_srvs - 1`]}

    cd $SOCIAL_NET_DIR
    sed "s/constexpr uint32_t kNumEntries.*/constexpr uint32_t kNumEntries = $num_srvs;/g" -i src/main.cpp
    sed "s/constexpr static uint32_t kNumEntries.*/constexpr static uint32_t kNumEntries = $num_srvs;/g" \
	-i bench/client.cpp
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $mops;/g" \
	-i bench/client.cpp
    cd build
    make clean
    make -j
    cd ..

    for srv_idx in `seq 1 $num_srvs`
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
    for srv_idx in `seq 1 $num_srvs`
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

    for srv_idx in `seq 1 $num_srvs`
    do
	if [[ $srv_idx -ne $num_srvs ]]
	then
	    start_server build/src/main $srv_idx $LPID &
	else
	    sleep 5
	    start_main_server build/src/main $srv_idx $LPID >$DIR/logs/.tmp &
	fi
    done
    ( tail -f -n0 $DIR/logs/.tmp & ) | grep -q "Starting the ThriftBackEndServer"
    sleep 5

    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"
    sleep 5

    client_pids=
    for clt_idx in `seq $CLT_START_IDX $CLT_END_IDX`
    do
	conf=$DIR/conf/client`expr $clt_idx - $CLT_START_IDX + 1`
	run_program build/bench/client $clt_idx $conf 1>$DIR/logs/$num_srvs.$clt_idx 2>&1 &
	client_pids+=" $!"
    done
    wait $client_pids

    cleanup
    sleep 5
done

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down_nginx.sh;"
run_cmd $NGINX_SRV_IDX "docker rm -vf $(docker ps -aq)"
run_cmd $NGINX_SRV_IDX "docker volume prune -f"
run_cmd $NGINX_SRV_IDX "sudo ip addr delete $NGINX_SRV_CALADAN_IP_AND_MASK dev $nic_dev"
