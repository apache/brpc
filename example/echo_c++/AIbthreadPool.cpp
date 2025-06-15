// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// A client sending requests to server every 1 second.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#include "echo.pb.h"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <chrono>
#include <atomic>
using namespace std;
DEFINE_string(attachment, "", "Carry this along with requests");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_int32(interval_ms, 1000, "Milliseconds between consecutive requests");



template<typename T>
class BthreadPool {
private:
    int worker_count;
    vector<bthread_t> workers;
    queue<T> task_queue;
    bthread_mutex_t queue_mutex;
    bthread_cond_t queue_cond;
    atomic_bool stop{false};

public:
    BthreadPool(size_t threads) : worker_count(threads) {
        bthread_mutex_init(&queue_mutex, NULL);
        bthread_cond_init(&queue_cond, NULL);
        workers.resize(threads);
    }

    void start() {
        for (int i = 0; i < worker_count; ++i) {
            bthread_start_background(&workers[i], NULL, &BthreadPool::worker_thread, this);
        }
    }

    static void* worker_thread(void* arg) {
        BthreadPool* pool = static_cast<BthreadPool*>(arg);
        while (true) {
            T task;
            {
                bthread_mutex_lock(&pool->queue_mutex);
                while (pool->task_queue.empty() && !pool->stop) {
                    bthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
                }
                
                if (pool->stop && pool->task_queue.empty())
                    break;
                    
                task = move(pool->task_queue.front());
                pool->task_queue.pop();
                bthread_mutex_unlock(&pool->queue_mutex);
            }
            task(); // 执行任务
        }
        bthread_mutex_unlock(&pool->queue_mutex);
        return NULL;
    }

    void enqueue(T task) {
        bthread_mutex_lock(&queue_mutex);
        task_queue.push(move(task));
        bthread_cond_signal(&queue_cond);
        bthread_mutex_unlock(&queue_mutex);
    }

    void shutdown() {
        stop = true;
        bthread_mutex_lock(&queue_mutex);
        bthread_cond_broadcast(&queue_cond);
        bthread_mutex_unlock(&queue_mutex);
        
        for (bthread_t& worker : workers) {
            bthread_join(worker, NULL);
        }
        
        bthread_mutex_destroy(&queue_mutex);
        bthread_cond_destroy(&queue_cond);
    }
};

void *fun(void *args){
    // std::cout<<"fun test test test test test:\n";
    return nullptr;
}

void test(int i){
    // std::cout<<"fun:"<<i<<endl;;
}
int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    

    BthreadPool<function<void()>>  bthreadPool(200000);
    bthreadPool.start();
    auto start = std::chrono::high_resolution_clock::now();
    for(int i=0;i<10000;i++){
        bthreadPool.enqueue(bind(test,i));
        // this_thread::sleep_for(chrono::milliseconds(1));
    }
    bthreadPool.shutdown();
    auto over = std::chrono::high_resolution_clock::now();
    // 计算时间差（毫秒）
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(over - start);
    long time_diff_ms = duration.count();

    // 打印时间差
    std::cout << "Time taken by function: " << time_diff_ms << " milliseconds" << std::endl;

    return 0;
}

//1
//Time taken by function: 55 milliseconds
//10
//Time taken by function: 146 milliseconds
//16
// Time taken by function: 141 milliseconds
//500
// Time taken by function: 156 milliseconds
//1000
//Time taken by function: 150 milliseconds
// 5000
//Time taken by function: 181 milliseconds
//10000
//Time taken by function: 192 milliseconds
//15000
//Time taken by function: 158 milliseconds
//20000
//Time taken by function: 447 milliseconds
//50000
//Time taken by function: 493 milliseconds
//100000
//Time taken by function: 643 milliseconds


// E0611 14:34:38.983116 90092     0 src/bthread/task_group.cpp:814] _remote_rq is full, capacity=2048
// E0611 14:34:39.984859 90092     0 src/bthread/task_group.cpp:814] _remote_rq is full, capacity=2048
// E0611 14:34:40.985313 90092     0 src/bthread/task_group.cpp:814] _remote_rq is full, capacity=2048
// E0611 14:34:41.986268 90092     0 src/bthread/task_group.cpp:814] _remote_rq is full, capacity=2048

// E0611 14:36:25.037725 90387 4295112088 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096
// E0611 14:36:26.038123 90389 4295174780 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096
// E0611 14:36:27.037936 90389 4295174780 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096
// E0611 14:36:28.038803 90389 4295174780 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096
// E0611 14:36:29.038905 90382 4295218975 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096
// E0611 14:36:30.039379 90385 4295020568 ./src/bthread/task_group_inl.h:95] _rq is full, capacity=4096