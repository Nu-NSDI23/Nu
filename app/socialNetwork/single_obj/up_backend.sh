#!/bin/bash

NU_CTRL_IP=18.18.1.3

sudo ../../../caladan/iokerneld >logs/iokerneld.srv 2>&1 &
sleep 5

sudo build/src/main ../../../conf/controller CTL $NU_CTRL_IP >logs/controller 2>&1 &
sleep 3
sudo build/src/main ../../../conf/server1 SRV $NU_CTRL_IP >logs/server 2>&1 &
sleep 3
sudo build/src/main ../../../conf/client1 CLT $NU_CTRL_IP >logs/client 2>&1 &
sleep 5
