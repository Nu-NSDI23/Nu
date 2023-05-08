#!/bin/bash

sudo apt update && sudo apt install -y cmake g++ wget unzip

cd opencv
rm -rf build
mkdir build
rm -rf install
mkdir install

cd build
cmake -DCMAKE_BUILD_TYPE=Release -DWITH_IPP=OFF -DWITH_TBB=OFF \
      -DWITH_OPENMP=OFF -DWITH_PTHREADS_PF=OFF -DCV_TRACE=OFF \
      -DCMAKE_INSTALL_PREFIX=../install ..
cmake --build . --target install -j
