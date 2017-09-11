#!/bin/sh

make -sj8 || exit -1

function exit_func {
    kill -- -$$
    echo "Killed all"
    exit 0
}
trap "exit_func" EXIT SIGINT SIGTERM

echo > server_list
starting_port=8004
pids=()
num_servers=5
interval=2

for ((i = 0; i < $num_servers; ++i)); do
    ./echo_server -server_num 1 -port $((starting_port+i)) &
    pids[$i]=$!
done
usleep 100000
./echo_client -use_bthread -thread_num 400 -timeout_ms -1 --health_check_interval 1 -dont_fail > echo_client.log 2>&1 &

echo ${pids[*]}

while true; do
    echo "Running for $interval seconds..."
    sleep $interval
    index=$((RANDOM%num_servers))
    server_pid=${pids[$index]}
    echo "Kill $server_pid(index=$index)"
    kill -INT $server_pid
    echo "Wait for another $interval seconds..."
    sleep $interval
    if grep -q "BAD\!\|^FATAL:" echo_client.log; then
        echo "************** Found bug! ***************"
        echo "Check echo_client.log for client logs"
        exit 1
    fi
    ./echo_server -server_num 1 -port $((starting_port+index)) &
    pids[$index]=$!
    echo "Create echo_server=${pids[$index]} at index=$index"
done
