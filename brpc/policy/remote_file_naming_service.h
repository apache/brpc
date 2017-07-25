// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/20 14:10:19

#ifndef  BRPC_POLICY_HTTP_FILE_NAMING_SERVICE
#define  BRPC_POLICY_HTTP_FILE_NAMING_SERVICE

#include "brpc/periodic_naming_service.h"
#include "brpc/channel.h"
#include "base/unique_ptr.h"


namespace brpc {
class Channel;
namespace policy {

class RemoteFileNamingService : public PeriodicNamingService {
private:
    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;
    
    void Destroy();
    
private:
    std::unique_ptr<Channel> _channel;
    std::string _server_addr;
    std::string _path;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_HTTP_FILE_NAMING_SERVICE
