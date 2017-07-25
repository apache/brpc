// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Feb  4 16:47:12 CST 2016

#ifndef BRPC_TRACKME_H
#define BRPC_TRACKME_H

// [Internal] RPC users are not supposed to call functions below. 

#include "base/endpoint.h"


namespace brpc {

// Set the server address for reporting.
// Currently only the first address will be saved.
void SetTrackMeAddress(base::EndPoint pt);

// Call this function every second (or every several seconds) to send
// TrackMeRequest to trackme_server every TRACKME_INTERVAL seconds.
void TrackMe();

} // namespace brpc


#endif // BRPC_TRACKME_H
