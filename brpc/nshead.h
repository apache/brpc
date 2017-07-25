// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Feb  9 21:21:01 2015

#ifndef BRPC_NSHEAD_H
#define BRPC_NSHEAD_H


namespace brpc {

// Copied from public/nshead/nshead.h which is essentially unchangable. (Or
// even if it's changed, servers accepting new formats should also accept
// older formats).
static const unsigned int NSHEAD_MAGICNUM = 0xfb709394;
struct nshead_t {
    unsigned short id;
    unsigned short version;       
    unsigned int   log_id;
    char           provider[16];
    unsigned int   magic_num;
    unsigned int   reserved;       
    unsigned int   body_len;
};

} // namespace brpc


#endif // BRPC_NSHEAD_H
