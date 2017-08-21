// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// File baidu_naming_service.h
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date 2014/10/20 11:42:51
// Implement NamingService to acquire server list from Baidu-Naming-Service

#ifdef BAIDU_INTERNAL

#ifndef  BRPC_POLICY_BAIDU_NAMING_SERVICE_H
#define  BRPC_POLICY_BAIDU_NAMING_SERVICE_H

#include "brpc/periodic_naming_service.h"

namespace brpc {
namespace policy {

class BaiduNamingService : public PeriodicNamingService {
public:
    // You can specify port by appending port selector:
    // e.g.: bns://DPOP-inner-API-inner-API.jpaas.hosts:main
    //                                                 ^^^^^
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);
    
    void Describe(std::ostream& os, const DescribeOptions&) const;
    
    NamingService* New() const;
    
    void Destroy();
};

}  // namespace policy
} // namespace brpc

#endif //BRPC_POLICY_BAIDU_NAMING_SERVICE_H
#endif // BAIDU_INTERNAL
