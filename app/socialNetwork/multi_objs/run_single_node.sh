NGINX_NODE=amd260.utah.cloudlab.us
CLIENT_NODE=amd251.utah.cloudlab.us
WORKPLACE=`pwd`
MIN_MOPS=0.1
MAX_MOPS=1.0
STEP_MOPS=0.1

ssh $CLIENT_NODE "sudo pkill -9 -x iokerneld"
ssh $CLIENT_NODE "sudo pkill -9 -x client"
ssh $NGINX_NODE "cd $WORKPLACE; ./down_nginx.sh; ./up_nginx.sh"

for mops in `seq $MIN_MOPS $STEP_MOPS $MAX_MOPS`
do
    ./down_backend.sh
    ./up_backend.sh
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
