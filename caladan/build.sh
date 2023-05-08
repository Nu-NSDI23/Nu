#!/bin/bash

sudo apt-get install -y make gcc cmake pkg-config libnl-3-dev libnl-route-3-dev  \
                        libnuma-dev uuid-dev libssl-dev libaio-dev libcunit1-dev \
                        libclang-dev libncurses-dev meson python-dev python3-pyelftools

make submodules -j
make clean && make -j
pushd ksched
make clean && make -j
popd
pushd bindings/cc/
make -j
popd
sudo ./scripts/setup_machine.sh
