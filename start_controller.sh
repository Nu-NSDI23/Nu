#!/bin/bash

sudo pkill iokerneld
sudo pkill ctrl_main

source setup.sh >/dev/null 2>&1
sudo sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

sudo bash -c "caladan/iokerneld ias > /dev/null 2>&1 &"
sudo stdbuf -o0 sh -c "bin/ctrl_main" 1>/dev/null 2>&1 &
disown -r
sleep 3

# run main server
# sudo stdbuf -o0 sh -c "binary -m -l 1 -i 18.18.1.3"

# run server
# sudo stdbuf -o0 sh -c "binary -l 1 -i 18.18.1.2"
