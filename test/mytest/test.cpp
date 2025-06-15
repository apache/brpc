#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bthread/bthread.h"

// 线程间共享的数据结构
typedef struct {
    bthread_mutex_t mutex;
    bthread_cond_t cond;
    int shared_value;
} SharedData;

// 线程本地存储的键
static bthread_key_t tls_key;

// 一次性初始化函数
static bthread_once_t once_control = BTHREAD_ONCE_INIT;
void init_tls_key(void) {
    bthread_key_create(&tls_key, free);
    printf("TLS key created\n");
}

// 工作线程函数
static void* worker_func(void* arg) {
    SharedData* data = (SharedData*)arg;
    
    // 一次性初始化TLS
    bthread_once(&once_control, init_tls_key);
    
    // 设置线程本地存储
    bthread_t* thread_id = (bthread_t*)malloc(sizeof(bthread_t));
    *thread_id = bthread_self();
    bthread_setspecific(tls_key, thread_id);
    
    printf("Thread %lu started\n", (unsigned long)*thread_id);
    
    // 互斥锁使用
    bthread_mutex_lock(&data->mutex);
    data->shared_value++;
    printf("Thread %lu updated shared value to %d\n", 
           (unsigned long)*thread_id, data->shared_value);
    
    // 条件变量等待
    while (data->shared_value < 3) {
        printf("Thread %lu waiting for condition\n", (unsigned long)*thread_id);
        bthread_cond_wait(&data->cond, &data->mutex);
    }
    bthread_mutex_unlock(&data->mutex);
    
    // 模拟工作
    for (int i = 0; i < 3; i++) {
        printf("Thread %lu working...\n", (unsigned long)*thread_id);
        bthread_usleep(100000); // 100ms
        
        // 检查是否被中断
        if (bthread_stopped(bthread_self())) {
            printf("Thread %lu received stop signal!\n", (unsigned