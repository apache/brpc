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
class BthreadPool{
private:
    int size;
    vector<thread> pool;
    vector<bthread_t> bPool;
    vector<int> poolint;
    queue<T> tasks;
    mutex mu,mu2;
    condition_variable cv;
    atomic_bool flag;
public:
    BthreadPool(int size){
        this->size=size;
        flag=false;
        bPool.resize(size);
    }
    void start(){
        // pool=vector<thread>(size);
        flag=true;
        for(int i=0;i<size;i++){
            bthread_start_background(&bPool[i], NULL, &BthreadPool::work, this);
            // pool.emplace_back(thread(&BthreadPool::work, this, i));
        }
    }
    void apend(T task){
        unique_lock<mutex> lock(mu);
        tasks.push(task);
        cv.notify_one();
    }
    static void *work(void* arg){
        BthreadPool* self = static_cast<BthreadPool*>(arg);
        while(self->flag||!self->tasks.empty()){
            T fun;
            unique_lock<mutex> lock(self->mu);
            while(self->flag&&self->tasks.empty()){
                self->cv.wait(lock);
            }
            if(!self->tasks.empty()){
                fun=self->tasks.front();
                self->tasks.pop();
                lock.unlock();
                self->mu2.lock();
                cout<<"bthread"<<":";
                fun();
                self->mu2.unlock();
                lock.lock();
            }
        }
        return nullptr;
    }
    void over(){
        flag=false;
        cv.notify_all();
        for(int i=0;i<size;i++){
            cout<<bPool[i]<<";";
            bthread_join(bPool[i],NULL);
        }
    }
    ~BthreadPool(){
        if(flag){
            over();
        }
    }
    
};

void *fun(void *args){
    std::cout<<"fun test test test test test:\n";
    return nullptr;
}

void test(int i){
    std::cout<<"fun:"<<i<<endl;;
}
int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    

    BthreadPool<function<void()>>  bthreadPool(10000);
    bthreadPool.start();
    auto start = std::chrono::high_resolution_clock::now();
    for(int i=0;i<10000;i++){
        bthreadPool.apend(bind(test,i));
        // this_thread::sleep_for(chrono::milliseconds(1));
    }
    bthreadPool.over();
    auto over = std::chrono::high_resolution_clock::now();
    // 计算时间差（毫秒）
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(over - start);
    long time_diff_ms = duration.count();

    // 打印时间差
    std::cout << "Time taken by function: " << time_diff_ms << " milliseconds" << std::endl;

    return 0;
}


//20
//Time taken by function: 267 milliseconds
//500
//Time taken by function: 211 milliseconds
//1000
//Time taken by function: 171 milliseconds
// 5000
// Time taken by function: 191 milliseconds
//10000
//Time taken by function: 149 milliseconds
//15000
//Time taken by function: 154 milliseconds
//1
//Time taken by function: 77 milliseconds
//16
//Time taken by function: 136 milliseconds
