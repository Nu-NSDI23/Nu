#!/bin/bash

source ../../shared.sh

DISK_DEV=/dev/nvme1n1

DIR=`pwd`
DOCKER_MASTER_IDX=32
DOCKER_MASTER_IP=$(ssh_ip $DOCKER_MASTER_IDX)
CLT_IDX=31
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/orig/
MOPS=0.4

cd $SOCIAL_NET_DIR
./build.sh

mount_docker_folder_cmd="sudo service docker stop;
                         echo N | sudo mkfs.ext4 $DISK_DEV;
                         sudo umount /mnt;
                         sudo mount $DISK_DEV /mnt;
                         sudo mkdir /mnt/docker;
                         sudo mount --rbind /mnt/docker /var/lib/docker;
                         sudo rm -rf /var/lib/docker/*;
                         sudo service docker start;"
run_cmd $DOCKER_MASTER_IDX $mount_docker_folder_cmd

for num_srvs in `seq 1 30`
do
    run_cmd $DOCKER_MASTER_IDX "cd $SOCIAL_NET_DIR; ./install_docker.sh"
    join_cmd=`run_cmd $DOCKER_MASTER_IDX "docker swarm init --advertise-addr $DOCKER_MASTER_IP" | grep token | head -n 1`
    docker_master_hostname=$(run_cmd $DOCKER_MASTER_IDX "docker node ls | tail -n 1 | awk '{print \$3}'")
    run_cmd $DOCKER_MASTER_IDX "docker node update --availability drain $docker_master_hostname"
    for srv_idx in `seq 1 $num_srvs`
    do
	run_cmd $srv_idx $mount_docker_folder_cmd
	run_cmd $srv_idx "cd $SOCIAL_NET_DIR; ./install_docker.sh"
    run_cmd $srv_idx "$join_cmd"
    done

    cd $SOCIAL_NET_DIR
    run_cmd $DOCKER_MASTER_IDX "cd $SOCIAL_NET_DIR; docker stack deploy --compose-file=docker-compose-swarm.yml socialnet"
    sleep 60

    for srv_idx in `seq 1 $num_srvs`
    do
	has_openresty=`run_cmd $srv_idx "docker container ls | grep openresty | wc -l"`
	has_composepost=`run_cmd $srv_idx "docker container ls | grep ComposePostService | wc -l"`
	has_usertimeline=`run_cmd $srv_idx "docker container ls | grep UserTimelineService | wc -l"`
	has_socialgraph=`run_cmd $srv_idx "docker container ls | grep SocialGraphService | wc -l"`
	has_hometimeline=`run_cmd $srv_idx "docker container ls | grep HomeTimelineService | wc -l"`
	if [[ $has_openresty -ne 0 ]]; then
	    run_cmd $srv_idx "sudo apt-get update; sudo apt-get install -y python3-pip; pip3 install aiohttp"
	    run_cmd $srv_idx "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"
	fi
	ip_100g=`run_cmd $srv_idx "ifconfig $nic_dev" | grep "inet " | awk '{print $2}'`
	if [[ $has_composepost -ne 0 ]]; then
	    composepost_ip=$ip_100g
	fi
	if [[ $has_usertimeline -ne 0 ]]; then
	    usertimeline_ip=$ip_100g
	fi
	if [[ $has_socialgraph -ne 0 ]]; then
	    socialgraph_ip=$ip_100g
	fi
	if [[ $has_hometimeline -ne 0 ]]; then
	    hometimeline_ip=$ip_100g
	fi
    done

    sed "s/constexpr static char kUserTimeLineIP.*/constexpr static char kUserTimeLineIP[] = \"$usertimeline_ip\";/g" \
	-i src/ClientSwarm/client_swarm.cpp
    sed "s/constexpr static char kHomeTimeLineIP.*/constexpr static char kHomeTimeLineIP[] = \"$hometimeline_ip\";/g" \
	-i src/ClientSwarm/client_swarm.cpp
    sed "s/constexpr static char kComposePostIP.*/constexpr static char kComposePostIP[] = \"$composepost_ip\";/g" \
	-i src/ClientSwarm/client_swarm.cpp
    sed "s/constexpr static char kSocialGraphIP.*/constexpr static char kSocialGraphIP[] = \"$socialgraph_ip\";/g" \
	-i src/ClientSwarm/client_swarm.cpp
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $MOPS;/g" \
	-i src/ClientSwarm/client_swarm.cpp
    cd build
    make clean
    make -j

    cd src/ClientSwarm
    run_cmd $CLT_IDX "mkdir -p `pwd`"
    distribute client_swarm $CLT_IDX
    start_iokerneld $CLT_IDX
    sleep 5
    run_program client_swarm $CLT_IDX $DIR/conf/client.conf 1>$DIR/logs/$num_srvs 2>&1

    run_cmd $DOCKER_MASTER_IDX "docker stack rm socialnet"
    sleep 15
    for srv_idx in `seq 1 $num_srvs`
    do
        run_cmd $srv_idx "docker swarm leave --force"
    done
    run_cmd $DOCKER_MASTER_IDX "docker swarm leave --force"
    for srv_idx in `seq 1 $num_srvs`
    do
	run_cmd $srv_idx 'docker rm -vf $(docker ps -aq)'
	run_cmd $srv_idx "docker volume prune -f"
    done

    cleanup
    sleep 5
done
