// Copyright (c) 2018 abstraction No Inc 
// Author: hairet (hairet@vip.qq.com)
// Date: 2018/11/10 

#include <gtest/gtest.h>
#include "butil/compat.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/string_printf.h"
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/barrel.h" 
#include "butil/gperftools_profiler.h"
#include <sys/types.h>
#include <map>
#include <vector>

#define gettid() syscall(__NR_gettid) 

using namespace std;

int c = 0;
int register_idx = 0;

#define MAX_THREAD 1000

long register_idx_tid[2][MAX_THREAD] = {0};
long run_idx_tid[2][MAX_THREAD] = {0};

long start_time = butil::cpuwide_time_ms();

struct t_arg {
    uint64_t uid;
    long *register_idx_tid;
    long *run_idx_tid;
};

void* barreler(void* arg) {
    t_arg *p_arg = (t_arg *)arg;
    int reg_idx = ++register_idx;
    bthread::UinBarrel::GetInstance()->LockUinBarrel(p_arg->uid);
    int run_idx = ++c;
    p_arg->register_idx_tid[reg_idx] = (long)gettid();
    p_arg->run_idx_tid[run_idx] = (long)gettid();
    printf("[%" PRIu64 "] I'm here barreler, uid:%lld, run_idx:%d, reg_idx:%d %" PRId64 "ms\n",
               pthread_numeric_id(), (long long int)p_arg->uid, run_idx, reg_idx, butil::cpuwide_time_ms() - start_time);
    bthread_usleep(100000);
    printf("[%" PRIu64 "] I'm exit barreler, uid:%lld, run_idx:%d, reg_idx:%d %" PRId64 "ms\n",
               pthread_numeric_id(), (long long int)p_arg->uid, run_idx, reg_idx, butil::cpuwide_time_ms() - start_time);
    bthread::UinBarrel::GetInstance()->UnLockUinBarrel(p_arg->uid);
    return NULL;
}

TEST(BthreadBarrelTest, sanity) {
    uint64_t uid1 = 1357890;
    uint64_t uid2 = 1357891;
    pthread_t th[8];
    t_arg arg[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        arg[i].uid = ((i%2==0)?uid1:uid2);
        arg[i].register_idx_tid = register_idx_tid[i%2];
        arg[i].run_idx_tid = run_idx_tid[i%2];
        ASSERT_EQ(0, pthread_create(&th[i], NULL, barreler, &arg[i]));
        usleep(1500);  //sleep make pthread create and in barrel 
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        pthread_join(th[i], NULL);
    }
    for(unsigned int k = 0; k<2; ++k) {
        for(unsigned int i = 1; i<ARRAY_SIZE(th)+1 && i<MAX_THREAD; ++i) {
            ASSERT_EQ(register_idx_tid[k][i], run_idx_tid[k][i]);
        }
    }
}


