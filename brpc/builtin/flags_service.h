// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 16:27:41 2015

#ifndef BRPC_FLAGS_SERVICE_H
#define BRPC_FLAGS_SERVICE_H

#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/tabbed.h"


namespace brpc {

class FlagsService : public flags, public Tabbed {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::FlagsRequest* request,
                        ::brpc::FlagsResponse* response,
                        ::google::protobuf::Closure* done);

    void GetTabInfo(TabInfoList* info_list) const;

private:
    void set_value_page(Controller* cntl, ::google::protobuf::Closure* done);

};

} // namespace brpc



#endif // BRPC_FLAGS_SERVICE_H

