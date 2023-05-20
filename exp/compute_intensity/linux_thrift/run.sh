#!/bin/bash

source ../../shared.sh

DELAYS=( 100 1000 2000 3000 4000 5000 6000 7000 8000 9000 10000 )
CLT_IDX=1
SRV_IDX=2
SRV_IP=$(ssh_ip $SRV_IDX)

version=0.12.0 && git clone -b $version https://github.com/apache/thrift.git
cd thrift
./bootstrap.sh
./configure --enable-nuthreads=no --enable-caladantcp=no \
            --enable-shared=no --enable-tests=no --enable-tutorial=no \
	    --with-libevent=no
make -j
cd ..

make clean
sed "s/\(constexpr auto kIp = \).*/\1 \"$SRV_IP\";/g" -i client.cpp

for delay in ${DELAYS[@]}
do
    sed "s/\(constexpr uint32_t kDelayNs = \).*/\1$delay;/g" -i server.cpp
    make

    distribute server $SRV_IDX

    run_program server $SRV_IDX &
    sleep 5

    run_program client $CLT_IDX 1>logs/$delay 2>&1 &
    sleep 10

    cleanup
    sleep 5
done
