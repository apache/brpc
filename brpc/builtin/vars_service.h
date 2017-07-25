// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 18:07:12 2015

#ifndef BRPC_VARS_SERVICE_H
#define BRPC_VARS_SERVICE_H

#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/tabbed.h"


namespace brpc {

class VarsService : public vars, public Tabbed {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::VarsRequest* request,
                        ::brpc::VarsResponse* response,
                        ::google::protobuf::Closure* done);

    void GetTabInfo(TabInfoList* info_list) const;
};

} // namespace brpc


#endif // BRPC_VARS_SERVICE_H
