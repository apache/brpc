// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/20 14:10:19

#ifndef  BRPC_POLICY_FILE_NAMING_SERVICE
#define  BRPC_POLICY_FILE_NAMING_SERVICE

#include "brpc/naming_service.h"


namespace brpc {
namespace policy {

class FileNamingService : public NamingService {
private:
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);
    
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;
    
    void Destroy();
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_FILE_NAMING_SERVICE
