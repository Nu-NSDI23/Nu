#!/bin/bash

pushd ../thrift
./bootstrap.sh
./configure --enable-nuthreads=yes --enable-caladantcp=yes \
            --with-nu=`pwd`/../../../  \
            --enable-shared=no --enable-tests=no --enable-tutorial=no \
	    --with-libevent=no
make -j
popd

mkdir build
cd build
cmake ..
make -j
cd ..
