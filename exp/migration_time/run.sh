#!/bin/bash

source ../shared.sh

HEAP_SIZES=( 65536 131072 262144 524288 1048576 2097152 4194304 8388608 16777216 )
LPID=1
SRC_SRV_IDX=1
DEST_SRV_IDX=2

pushd $NU_DIR
sed "s/\(constexpr static bool kEnableLogging =\).*/\1 true;/g" -i src/migrator.cpp
make -j`nproc`
popd

make clean

for heap_size in ${HEAP_SIZES[@]}
do
    start_iokerneld $SRC_SRV_IDX
    start_iokerneld $DEST_SRV_IDX
    sleep 5
 
    sed "s/\(constexpr uint32_t kObjSize =\).*/\1 $heap_size;/g" -i main.cpp
    make
    distribute main $SRC_SRV_IDX
    distribute main $DEST_SRV_IDX

    start_ctrl $SRC_SRV_IDX
    sleep 5

    start_server main $SRC_SRV_IDX $LPID 1>logs/$heap_size.src 2>&1 &
    start_server main $DEST_SRV_IDX $LPID 1>logs/$heap_size.dest 2>&1 &
    sleep 5

    start_main_server main $SRC_SRV_IDX $LPID

    cleanup
    sleep 5
done

pushd $NU_DIR
sed "s/\(constexpr static bool kEnableLogging =\).*/\1 false;/g" -i src/migrator.cpp
make -j
popd
