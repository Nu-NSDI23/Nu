#!/bin/bash

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
NU_DIR=$SCRIPT_DIR/..
CALADAN_DIR=$NU_DIR/caladan

function ssh_ip() {
    srv_idx=$1
    echo "10.10.2."$srv_idx
}

function caladan_srv_ip() {
    srv_idx=$1
    echo "18.18.1."$(($srv_idx+1))
}

function caladan_main_srv_ip() {
    srv_idx=$1
    echo "18.18.1."$(($srv_idx+128))
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

function cleanup() {
    for i in `seq 1 $num_nodes`
    do
	ssh $(ssh_ip $i) "sudo pkill -9 iokerneld; \
                          sudo pkill -9 ctrl_main;
                          sudo pkill -9 main;
                          sudo pkill -9 client;
                          sudo pkill -9 server;"
    done
}

function force_cleanup() {
    cleanup
    sudo pkill -9 run.sh
    exit 1
}

function start_iokerneld() {
    srv_idx=$1
    ssh $(ssh_ip $srv_idx) "sudo $CALADAN_DIR/iokerneld" &
}

function start_ctrl() {
    srv_idx=$1
    ssh $(ssh_ip $srv_idx) "sudo $NU_DIR/bin/ctrl_main" &
}

function __start_server() {
    file_path=$1
    file_full_path=$(readlink -f $file_path)
    srv_idx=$2
    lpid=$3
    main=$4
    if [[ $main -eq 0 ]]
    then
	ip=$(caladan_srv_ip $srv_idx)
	ssh $(ssh_ip $srv_idx) "sudo $file_full_path -l $lpid -i $ip"
    else
	ip=$(caladan_main_srv_ip $srv_idx)
	ssh $(ssh_ip $srv_idx) "sudo $file_full_path -m -l $lpid -i $ip"
    fi
}

function start_server() {
    __start_server $1 $2 $3 0
}

function start_main_server() {
    __start_server $1 $2 $3 1
}

function distribute() {
    file_path=$1
    file_full_path=$(readlink -f $file_path)
    src_idx=$2
    scp $file_full_path $(ssh_ip $src_idx):$file_full_path
}

function prepare() {
    probe_num_nodes
    for i in `seq 1 $num_nodes`
    do
	ssh $(ssh_ip $i) "cd $NU_DIR; sudo ./setup.sh" &
    done
    wait
}

trap force_cleanup INT
trap cleanup EXIT

prepare
cleanup
sleep 5

rm -rf logs.bak
[ -d logs ] && mv logs logs.bak
mkdir logs
