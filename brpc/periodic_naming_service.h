// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 20:02:37 CST 2014

#ifndef BRPC_PERIODIC_NAMING_SERVICE_H
#define BRPC_PERIODIC_NAMING_SERVICE_H

#include "brpc/naming_service.h"


namespace brpc {

class PeriodicNamingService : public NamingService {
protected:
    virtual int GetServers(const char *service_name,
                           std::vector<ServerNode>* servers) = 0;
    
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);
};

} // namespace brpc


#endif  // BRPC_PERIODIC_NAMING_SERVICE_H
