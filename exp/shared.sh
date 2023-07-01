#!/bin/bash

EXP_SHARED_SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
NU_DIR=$EXP_SHARED_SCRIPT_DIR/..
CALADAN_DIR=$NU_DIR/caladan

source $EXP_SHARED_SCRIPT_DIR/../setup.sh

function ssh_ip() {
    if [ -z "$ssh_ip_prefix" ]
    then
        ssh_ip_prefix=`ifconfig | grep "10\.10\." -B 1 --no-group-separator |
                                  sed 'N;s/\n/ /' | grep -v $nic_dev |
                                  awk '{print $6}' | head -c -2`
    fi
    srv_idx=$1
    echo $ssh_ip_prefix$srv_idx
}

function caladan_srv_ip() {
    srv_idx=$1
    echo "18.18.1."$(($srv_idx+1))
}

function probe_num_nodes() {
    num_nodes=1
    while true
    do
    	next_node=$(($num_nodes + 1))
	ping -c 1 $(ssh_ip $next_node) 1>/dev/null 2>&1
    	if [ $? != 0 ]
    	then
    	    break
    	fi
    	num_nodes=$next_node
    done
}

function executable_file_path() {
    if [[ "$1" = /* ]]
    then
	echo $1
    else
	echo "./$1"
    fi
}

function start_iokerneld() {
    srv_idx=$1
    ssh $(ssh_ip $srv_idx) "sudo $CALADAN_DIR/iokerneld" &
}

function start_ctrl() {
    srv_idx=$1
    ssh $(ssh_ip $srv_idx) "sudo stdbuf -o0 $NU_DIR/bin/ctrl_main" &
}

function __start_server() {
    file_path=$(executable_file_path $1)
    srv_idx=$2
    lpid=$3
    main=$4
    if [ -z "$5" ]
    then
	spin_ks=0
    else
	spin_ks=$5
    fi
    if [[ $6 -eq 0 ]]
    then
        isol_cmd=""
    else
	isol_cmd="--isol"
    fi
    ip=$(caladan_srv_ip $srv_idx)
    nu_libs_name=".nu_libs_$BASHPID"
    rm -rf $nu_libs_name
    mkdir $nu_libs_name
    cp `ldd $file_path | grep "=>" | awk  '{print $3}' | xargs` $nu_libs_name
    ssh $(ssh_ip $srv_idx) "rm -rf $nu_libs_name"
    scp -r $nu_libs_name $(ssh_ip $srv_idx):`pwd`/

    if [[ $main -eq 0 ]]
    then
	ssh $(ssh_ip $srv_idx) "cd `pwd`;
                                sudo LD_LIBRARY_PATH=$nu_libs_name stdbuf -o0 $file_path -l $lpid -i $ip -p $spin_ks $isol_cmd"
    else
	ssh $(ssh_ip $srv_idx) "cd `pwd`;
                                sudo LD_LIBRARY_PATH=$nu_libs_name stdbuf -o0 $file_path -m -l $lpid -i $ip -p $spin_ks $isol_cmd"
    fi
}

function start_server() {
    __start_server $1 $2 $3 0 $4 $5 0
}

function start_main_server() {
    __start_server $1 $2 $3 1 $4 $5 0
}

function start_main_server_isol() {
    __start_server $1 $2 $3 1 $4 $5 1
}

function run_program() {
    file_path=$(executable_file_path $1)
    srv_idx=$2
    args=${@:3}
    ssh $(ssh_ip $srv_idx) "cd `pwd`; sudo $file_path $args"
}

function run_cmd() {
    srv_idx=$1
    cmd=${@:2}
    ssh $(ssh_ip $srv_idx) "cd `pwd`; $cmd"
}

function distribute() {
    file_path=$1
    file_full_path=$(readlink -f $file_path)
    src_idx=$2
    scp $file_full_path $(ssh_ip $src_idx):$file_full_path
}

function prepare() {
    probe_num_nodes
    cleanup
    sleep 5
    for i in `seq 1 $num_nodes`
    do
	ssh $(ssh_ip $i) "cd $NU_DIR; sudo ./setup.sh" &
    done
    wait
}

function rebuild_caladan_and_nu() {
    pushd $CALADAN_DIR
    make clean
    make -j`nproc`
    cd bindings/cc
    make clean
    make -j`nproc`
    cd ../../..
    make clean
    make -j`nproc`
    popd
}

function caladan_use_small_rto() {
    small_rto=1
    pushd $CALADAN_DIR/runtime/net
    cp tcp.h tcp.h.bak
    sed "s/#define TCP_ACK_TIMEOUT.*/#define TCP_ACK_TIMEOUT ONE_MS/g" -i tcp.h
    sed "s/#define TCP_OOQ_ACK_TIMEOUT.*/#define TCP_OOQ_ACK_TIMEOUT ONE_MS/g" -i tcp.h
    sed "s/#define TCP_ZERO_WND_TIMEOUT.*/#define TCP_ZERO_WND_TIMEOUT ONE_MS/g" -i tcp.h
    sed "s/#define TCP_RETRANSMIT_TIMEOUT.*/#define TCP_RETRANSMIT_TIMEOUT ONE_MS/g" -i tcp.h
    rebuild_caladan_and_nu
    popd
}

function caladan_use_default_rto() {
    pushd $CALADAN_DIR/runtime/net
    git checkout -- tcp.h
    rebuild_caladan_and_nu
    popd
}

function cleanup() {
    for i in `seq 1 $num_nodes`
    do
	ssh $(ssh_ip $i) "sudo pkill -9 iokerneld;
                          sudo pkill -9 ctrl_main;
                          sudo pkill -9 main;
                          sudo pkill -9 client;
                          sudo pkill -9 server;
                          sudo pkill -9 synthetic;
                          sudo pkill -9 memcached;
                          sudo pkill -9 kmeans;
                          sudo pkill -9 python3;
                          sudo pkill -9 BackEndService;
                          sudo pkill -9 bench;"
	if [ -n "$nic_dev" ]
	then
            ssh $(ssh_ip $i) "sudo bridge fdb | grep $nic_dev | awk '{print $1}' | \
                              xargs -I {} bash -c \"sudo bridge fdb delete {} dev $nic_dev\""
	fi
	ssh $(ssh_ip $i) "cd `pwd`; rm -rf .nu_libs*"
    done
}

function force_cleanup() {
    echo -e "\nPlease wait for proper cleanups..."
    cleanup
    sudo pkill -9 run.sh
    exit 1
}

trap force_cleanup INT
trap cleanup EXIT

prepare
sleep 5

rm -rf logs.bak
[ -d logs ] && mv logs logs.bak
mkdir logs
