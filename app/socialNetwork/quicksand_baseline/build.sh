#!/bin/bash

sudo apt-get install libssl-dev libz-dev -y
sudo apt-get install automake bison flex libtool libssl-dev -y
sudo apt-get install libgtest-dev -y

mkdir build
cd build

git clone https://github.com/nlohmann/json.git
cd json
mkdir build
cd build
cmake -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=true -DJSON_BuildTests=OFF ..
make -j
sudo make install
cd ../..

pushd ../../thrift
./bootstrap.sh
./configure --enable-nuthreads=yes --enable-caladantcp=yes \
            --with-nu=`pwd`/../../../  \
            --enable-shared=no --enable-tests=no --enable-tutorial=no \
            --with-libevent=no
make -j
popd

git clone https://github.com/arun11299/cpp-jwt
cd cpp-jwt
mkdir build
cd build
cmake ..
make -j
sudo make install
cd ../..

cmake ..
make -j
