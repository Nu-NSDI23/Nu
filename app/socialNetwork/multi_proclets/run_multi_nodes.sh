NGINX_NODE=amd270.utah.cloudlab.us
CLIENT_NODE=amd270.utah.cloudlab.us
SERVER1_NODE=amd276.utah.cloudlab.us
SERVER2_NODE=amd248.utah.cloudlab.us
WORKPLACE=`pwd`
MIN_MOPS=0.1
MAX_MOPS=1.5
STEP_MOPS=0.1

ssh $CLIENT_NODE "sudo pkill -9 -x iokerneld"
ssh $CLIENT_NODE "sudo pkill -9 -x client"
ssh $NGINX_NODE "cd $WORKPLACE; ./down_nginx.sh; ./up_nginx.sh"
ssh $SERVER1_NODE "sudo pkill -9 Back"
ssh $SERVER2_NODE "sudo pkill -9 Back"
ssh $SERVER1_NODE "mkdir -p $WORKPLACE/build/src"
ssh $SERVER2_NODE "mkdir -p $WORKPLACE/build/src"
scp $WORKPLACE/build/src/BackEndService $SERVER1_NODE:$WORKPLACE/build/src
scp $WORKPLACE/build/src/BackEndService $SERVER2_NODE:$WORKPLACE/build/src
mkdir -p logs
ssh $SERVER1_NODE "mkdir -p $WORKPLACE/logs"
ssh $SERVER2_NODE "mkdir -p $WORKPLACE/logs"

for mops in `seq $MIN_MOPS $STEP_MOPS $MAX_MOPS`
do
    sudo pkill -9 iokerneld &
    sudo pkill -9 Back &
    ssh $SERVER1_NODE "sudo pkill -9 iokerneld" &
    ssh $SERVER2_NODE "sudo pkill -9 iokerneld" &
    ssh $SERVER1_NODE "sudo pkill -9 Back" &
    ssh $SERVER2_NODE "sudo pkill -9 Back" &
    sleep 5
    sudo ../../../caladan/iokerneld &
    ssh $SERVER1_NODE "cd $WORKPLACE; sudo ../../../caladan/iokerneld" &
    ssh $SERVER2_NODE "cd $WORKPLACE; sudo ../../../caladan/iokerneld" &
    sleep 5
    sudo build/src/BackEndService ../../../conf/controller CTL 18.18.1.3 &
    sleep 5
    sudo build/src/BackEndService ../../../conf/server1 SRV 18.18.1.3 &
    ssh $SERVER1_NODE "cd $WORKPLACE; sudo build/src/BackEndService ../../../conf/server2 SRV 18.18.1.3" &
    ssh $SERVER2_NODE "cd $WORKPLACE; sudo build/src/BackEndService ../../../conf/server3 SRV 18.18.1.3" &
    sleep 5
    sudo build/src/BackEndService ../../../conf/client1 CLT 18.18.1.3 &
    sleep 5
    ssh $NGINX_NODE "cd $WORKPLACE; python3 scripts/init_social_graph.py"
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $mops;/g" \
	-i bench/client.cpp
    cd build
    make -j
    scp bench/client $CLIENT_NODE:$WORKPLACE
    cd ..
    ssh $CLIENT_NODE "sudo $WORKPLACE/../../../caladan/iokerneld " >logs/iokerneld.clt 2>&1 &
    sleep 5
    ssh $CLIENT_NODE "sudo $WORKPLACE/client $WORKPLACE/../../../conf/client2" >logs/$mops 2>&1
    ssh $CLIENT_NODE "sudo pkill -x iokerneld"
done
