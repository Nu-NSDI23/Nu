#!/bin/bash

source ../../shared.sh

DELAYS=( 100 1000 2000 3000 4000 5000 6000 7000 8000 9000 10000 )
CLT_IDX=1
SRV_IDX=2
SRV_IP=$(ssh_ip $SRV_IDX)

cp -r ../../../app/socialNetwork/thrift/ .
cd thrift
./bootstrap.sh
./configure --enable-nuthreads=yes --enable-caladantcp=yes \
            --with-nu=$NU_DIR \
            --enable-shared=no --enable-tests=no --enable-tutorial=no \
	    --with-libevent=no
make -j
cd ..

num_cores=$((`nproc` - 2))
sed "s/\(runtime_kthreads\).*/\1 $num_cores/g" -i conf/server1
sed "s/\(runtime_kthreads\).*/\1 $num_cores/g" -i conf/client1
distribute conf/server1
distribute conf/client1

make clean
for delay in ${DELAYS[@]}
do
    start_iokerneld $CLT_IDX
    start_iokerneld $SRV_IDX    
    sleep 5
    
    sed "s/\(constexpr uint32_t kDelayNs = \).*/\1$delay;/g" -i server.cpp
    make

    distribute server $SRV_IDX

    ssh $SRV_IP "cd `pwd`; sudo ./server conf/server1" &
    sleep 5

    sudo ./client conf/client1 1>logs/$delay 2>&1 &
    sleep 10

    cleanup
    sleep 5
done
