#!/bin/bash

NU_CTRL_IP=18.18.1.3

sudo ../../../caladan/iokerneld >logs/iokerneld.srv 2>&1 &
sleep 5

sudo build/src/BackEndService ../../../conf/controller CTL $NU_CTRL_IP >logs/BackEndService.ctl 2>&1 &
sleep 3
sudo build/src/BackEndService ../../../conf/server1 SRV $NU_CTRL_IP >logs/BackEndService.srv 2>&1 &
sleep 3
sudo build/src/BackEndService ../../../conf/client1 CLT $NU_CTRL_IP >logs/BackEndService.clt 2>&1 &
sleep 5
