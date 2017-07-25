// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

#ifndef BRPC_RPC_REPLAY_INFO_THREAD_H
#define BRPC_RPC_REPLAY_INFO_THREAD_H

#include <pthread.h>
#include <bvar/bvar.h>

namespace brpc {

struct InfoThreadOptions {
    bvar::LatencyRecorder* latency_recorder;
    bvar::Adder<int64_t>* sent_count;
    bvar::Adder<int64_t>* error_count;

    InfoThreadOptions()
        : latency_recorder(NULL)
        , sent_count(NULL)
        , error_count(NULL) {}
};

class InfoThread {
public:
    InfoThread();
    ~InfoThread();
    void run();
    bool start(const InfoThreadOptions&);
    void stop();
    
private:
    bool _stop;
    InfoThreadOptions _options;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
    pthread_t _tid;
};

} // brpc

#endif //BRPC_RPC_REPLAY_INFO_THREAD_H
