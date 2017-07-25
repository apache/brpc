// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Mar 30 13:11:01 2015

#include "base/class_name.h"
#include "brpc/nshead_service.h"
#include "brpc/details/method_status.h"


namespace brpc {

NsheadService::NsheadService() : _additional_space(0) {
    _status = new (std::nothrow) MethodStatus;
    LOG_IF(FATAL, _status == NULL) << "Fail to new MethodStatus";
}

NsheadService::NsheadService(const NsheadServiceOptions& options)
    : _status(NULL), _additional_space(options.additional_space) {
    if (options.generate_status) {
        _status = new (std::nothrow) MethodStatus;
        LOG_IF(FATAL, _status == NULL) << "Fail to new MethodStatus";
    }
}

NsheadService::~NsheadService() {
    delete _status;
    _status = NULL;
}

void NsheadService::Describe(std::ostream &os, const DescribeOptions&) const {
    os << base::class_name_str(*this);
}

void NsheadService::Expose(const base::StringPiece& prefix) {
    _cached_name = base::class_name_str(*this);
    if (_status == NULL) {
        return;
    }
    std::string s;
    s.reserve(prefix.size() + 1 + _cached_name.size());
    s.append(prefix.data(), prefix.size());
    s.push_back('_');
    s.append(_cached_name);
    _status->Expose(s);
}

} // namespace brpc

