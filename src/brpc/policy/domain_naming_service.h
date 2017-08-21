// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Jan  1 18:55:20 CST 2015

#ifndef  BRPC_POLICY_DOMAIN_NAMING_SERVICE_H
#define  BRPC_POLICY_DOMAIN_NAMING_SERVICE_H

#include "brpc/periodic_naming_service.h"
#include "base/unique_ptr.h"


namespace brpc {
namespace policy {

class DomainNamingService : public PeriodicNamingService {
public:
    DomainNamingService();

private:
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;
    
    void Destroy();

private:
    std::unique_ptr<char[]> _aux_buf;
    size_t _aux_buf_len;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_DOMAIN_NAMING_SERVICE_H
