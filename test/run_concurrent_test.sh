#!/bin/bash
# run_concurrent_test.sh

NUM_CLIENTS=$1
MESSAGE_COUNT=$2
for ((i=1; i<=$NUM_CLIENTS; i++))
do
    ../build/stress_client -h 127.0.0.1 -p 8888 -s 1024 -n $MESSAGE_COUNT &
done

wait
echo "所有客户端测试完成"