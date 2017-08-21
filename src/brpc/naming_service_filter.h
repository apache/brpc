// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu May  5 18:55:33 2016

#ifndef BRPC_NAMING_SERVICE_FILTER_H
#define BRPC_NAMING_SERVICE_FILTER_H

#include "brpc/naming_service.h"      // ServerNode


namespace brpc {

class NamingServiceFilter {
public:
    virtual ~NamingServiceFilter() {}

    // Return true to take this `server' as a candidate to issue RPC
    // Return false to filter it out
    virtual bool Accept(const ServerNode& server) const = 0;
};

} // namespace brpc



#endif // BRPC_NAMING_SERVICE_FILTER_H

