SERVER_NODE=amd260.utah.cloudlab.us
CLIENT_NODE=amd251.utah.cloudlab.us
WORKPLACE=`pwd`
MIN_MOPS=0.002
MAX_MOPS=0.012
STEP_MOPS=0.002

ssh $CLIENT_NODE "sudo pkill -9 -x iokerneld"
ssh $CLIENT_NODE "sudo pkill -9 -x client"

for mops in `seq $MIN_MOPS $STEP_MOPS $MAX_MOPS`
do
    ssh $SERVER_NODE "cd $WORKPLACE; ./down.sh; ./up.sh"
    ssh $SERVER_NODE "cd $WORKPLACE; python3 scripts/init_social_graph.py"
    sed "s/constexpr static double kTargetMops.*/constexpr static double kTargetMops = $mops;/g" \
	-i src/Client/client.cpp
    cd build
    make -j
    scp src/Client/client $CLIENT_NODE:$WORKPLACE
    cd ..
    ssh $CLIENT_NODE "sudo $WORKPLACE/../../../caladan/iokerneld " >logs/iokerneld.clt 2>&1 &
    sleep 5
    ssh $CLIENT_NODE "sudo $WORKPLACE/client $WORKPLACE/client.conf" >logs/$mops 2>&1
    ssh $CLIENT_NODE "sudo pkill -x iokerneld"
done
