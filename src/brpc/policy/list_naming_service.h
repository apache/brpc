// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Jan  1 18:55:20 CST 2015

#ifndef  BRPC_POLICY_LIST_NAMING_SERVICE
#define  BRPC_POLICY_LIST_NAMING_SERVICE

#include "brpc/naming_service.h"


namespace brpc {
namespace policy {

class ListNamingService : public NamingService {
private:
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);

    // We don't need a dedicated bthread to run this static NS.
    bool RunNamingServiceReturnsQuickly() { return true; }
    
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions& options) const;

    NamingService* New() const;
    
    void Destroy();
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_LIST_NAMING_SERVICE
