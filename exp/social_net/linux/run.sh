#!/bin/bash

source ../../shared.sh

DISK_DEV=/dev/nvme1n1

DIR=`pwd`
NGINX_SRV_IDX=1
CLT_IDX=2
SOCIAL_NET_DIR=$DIR/../../../app/socialNetwork/orig/

NGINX_SRV_100G_IP=`run_cmd $NGINX_SRV_IDX "ifconfig $nic_dev" | grep "inet " | awk '{print $2}'`
CALADAN_IP=`echo $NGINX_SRV_100G_IP | sed "s/\(.*\)\.\(.*\)\.\(.*\)\.\(.*\)/\1\.\2\.\3\.254/g"`
sed "s/host_addr.*/host_addr $CALADAN_IP/g" -i conf/client.conf

cd $SOCIAL_NET_DIR
sed "s/constexpr static char kHostIP.*/constexpr static char kHostIP[] = \"$NGINX_SRV_100G_IP\";/g" \
    -i src/Client/client.cpp
./build.sh

run_cmd $NGINX_SRV_IDX "sudo service irqbalance stop"
run_cmd $NGINX_SRV_IDX "cd $DIR/../../../caladan/scripts/; sudo ./set_irq_affinity 0-47 mlx5"
SET_NIC_CMD="sudo ifconfig $nic_dev down;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev adaptive-rx off;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev adaptive-tx off;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev rx-usecs 0;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev rx-frames 0;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev tx-usecs 0;"
SET_NIC_CMD+="sudo ethtool -C $nic_dev tx-frames 0;"
SET_NIC_CMD+="sudo ethtool -N $nic_dev rx-flow-hash udp4 sdfn;"
SET_NIC_CMD+="sudo sysctl net.ipv4.tcp_syncookies=1;"
SET_NIC_CMD+="sudo ifconfig $nic_dev up;"
run_cmd $NGINX_SRV_IDX "$SET_NIC_CMD"
run_cmd $NGINX_SRV_IDX "sudo apt-get update; sudo apt-get install -y python3-pip; pip3 install aiohttp"
run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./install_docker.sh"

run_cmd $NGINX_SRV_IDX "sudo service docker stop;
                        echo N | sudo mkfs.ext4 $DISK_DEV;
                        sudo umount /mnt;
                        sudo mount $DISK_DEV /mnt;
                        sudo mkdir /mnt/docker;
                        sudo mount --rbind /mnt/docker /var/lib/docker;
                        sudo rm -rf /var/lib/docker/*;
                        sudo service docker start;"

mops=( 0.002 0.003 0.005 0.007 0.009 )

for mop in ${mops[@]}
do
    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./up.sh"
    sleep 15
    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; python3 scripts/init_social_graph.py"

    cd $SOCIAL_NET_DIR
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $mop;/g" -i src/Client/client.cpp
    cd build
    make clean
    make -j

    cd src/Client/
    run_cmd $CLT_IDX "mkdir -p `pwd`"
    distribute client $CLT_IDX
    start_iokerneld $CLT_IDX
    sleep 5
    run_program client $CLT_IDX $DIR/conf/client.conf 1>$DIR/logs/$mop 2>&1

    run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down.sh"
    run_cmd $NGINX_SRV_IDX 'docker rm -vf $(docker ps -aq)'
    run_cmd $NGINX_SRV_IDX "docker volume prune -f"
    cleanup
    sleep 5
done

run_cmd $NGINX_SRV_IDX "cd $SOCIAL_NET_DIR; ./down.sh;"
