
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <chrono>
#include <atomic>
#include <iostream>
using namespace std;


template<typename T>
class BthreadPool{
private:
    int size;
    vector<thread> pool;
    queue<T> tasks;
    mutex mu,mu2;
    condition_variable cv;
    atomic_bool flag;
public:
    BthreadPool(int size){
        this->size=size;
        flag=false;
    }
    void start(){
        if (flag) return;  // 防止重复启动
        flag=true;
        for(int i=0;i<size;i++){
            pool.emplace_back(thread(&BthreadPool::work, this, i));
        }
    }
    void apend(T task){
        unique_lock<mutex> lock(mu);
        tasks.push(task);
        cv.notify_one();
    }
    void work(int i){
        while(flag||!tasks.empty()){
            unique_lock<mutex> lock(mu);
            while(flag&&tasks.empty()){
                cv.wait(lock);
            }
            if(!tasks.empty()){
                T fun=tasks.front();
                tasks.pop();
                lock.unlock();
                mu2.lock();
                // cout<<"bthread"<<i<<":";
                cout<<"bthread"<<":";
                fun();
                mu2.unlock();
                lock.lock();
            }
        }
    }
    void over(){
        flag=false;
        cv.notify_all();
        for(int i=0;i<size;i++){
            pool[i].join();
        }
    }
    ~BthreadPool(){
        if(flag){
            over();
        }
    }
    
};


void test(int i){
    std::cout<<"fun:"<<i<<endl;;
}
int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you

    BthreadPool<function<void()>>  bthreadPool(8);
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

//10000
//Time taken by function: 1661 milliseconds
//5000
// Time taken by function: 1157 milliseconds
//1000
//Time taken by function: 391 milliseconds
//100
//Time taken by function: 196 milliseconds
//16
// Time taken by function: 145 milliseconds
//1
// Time taken by function: 78 millisecond
//2
// Time taken by function: 142 milliseconds
//5
//Time taken by function: 141 milliseconds
//50
// Time taken by function: 233 milliseconds