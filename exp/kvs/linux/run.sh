#!/bin/bash

source ../../shared.sh

CLT_IDX=1
SRV_IDX=2
SRV_100G_IP=`run_cmd $SRV_IDX "ifconfig $nic_dev" | grep "inet " | awk '{print $2}'`
echo $SRV_100G_IP
DIR=`pwd`

run_cmd $SRV_IDX "sudo service irqbalance stop"
run_cmd $SRV_IDX "cd $DIR/../../../caladan/scripts/; sudo ./set_irq_affinity 0-47 mlx5"

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

run_cmd $SRV_IDX "$SET_NIC_CMD"
run_cmd $SRV_IDX "sudo apt-get install -y libevent-dev"
run_cmd $SRV_IDX "cd $DIR; wget http://www.memcached.org/files/memcached-1.6.12.tar.gz"
run_cmd $SRV_IDX "cd $DIR; tar xvf memcached-1.6.12.tar.gz"
run_cmd $SRV_IDX "cd $DIR/memcached-1.6.12; ./configure; make -j"
run_cmd $SRV_IDX "cd $DIR/memcached-1.6.12; \
                   sudo nice -n -20 ./memcached -u zainruan -t 48 -U 8888 -p 8888 \
                                                -c 32768 -m 32000 -b 32768 \
                                                -o hashpower=28,no_hashexpand,lru_crawler,lru_maintainer,idle_timeout=0" &

start_iokerneld $CLT_IDX
sleep 5

run_cmd $CLT_IDX "cd $CALADAN_DIR;
                  sed 's/CONFIG_LTO=.*/CONFIG_LTO=n/g' -i build/config;
                  make clean;
                  GCC=gcc-9 make -j;
                  curl https://sh.rustup.rs -sSf | sh -s -- -y;
                  source $HOME/.cargo/env;
                  rustup default nightly;
                  cd apps/synthetic/;
                  cargo clean;
                  cargo build --release;"

run_program $DIR/../../../caladan/apps/synthetic/target/release/synthetic $CLT_IDX \
     $SRV_100G_IP:8888 --config $DIR/conf/client --mode runtime-client \
     --protocol memcached --transport tcp --mpps 6 --samples 20 --threads 48 --start_mpps 0.2 \
     --runtime=5 --nvalues=53687091 --key_size 20 --value_size 2 >$DIR/logs/log

run_cmd $CLT_IDX "cd $CALADAN_DIR;
                  sed 's/CONFIG_LTO=.*/CONFIG_LTO=y/g' -i build/config;
                  make clean;
                  make -j;"
cleanup
