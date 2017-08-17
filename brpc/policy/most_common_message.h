// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Nov 13 21:37:29 CST 2014

#ifndef BRPC_POLICY_MOST_COMMON_MESSAGE_H
#define BRPC_POLICY_MOST_COMMON_MESSAGE_H

#include "base/object_pool.h"
#include "brpc/input_messenger.h"


namespace brpc {
namespace policy {

// Try to use this message as the intermediate message between Parse() and
// Process() to maximize usage of ObjectPool<MostCommonMessage>, otherwise
// you have to new the messages or use a separate ObjectPool (which is likely
// to waste more memory)
struct BAIDU_CACHELINE_ALIGNMENT MostCommonMessage : public InputMessageBase {
    base::IOBuf meta;
    base::IOBuf payload;
    PipelinedInfo pi;

    inline static MostCommonMessage* Get() {
        return base::get_object<MostCommonMessage>();
    }

    // @InputMessageBase
    void DestroyImpl() {
        meta.clear();
        payload.clear();
        pi.reset();
        base::return_object(this);
    }
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_MOST_COMMON_MESSAGE_H
