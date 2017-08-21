// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Aug 27 10:40:14 CST 2015

#ifndef BRPC_SELECTIVE_CHANNEL_H
#define BRPC_SELECTIVE_CHANNEL_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "brpc/socket_id.h"
#include "brpc/channel.h"


namespace brpc {

// A combo channel to split traffic to sub channels, aka "schan". The main
// purpose of schan is to load balance between groups of servers.
// SelectiveChannel is a fully functional Channel:
//   * synchronous and asynchronous RPC.
//   * deletable immediately after an asynchronous call.
//   * cancelable call_id (cancels all sub calls).
//   * timeout.
// Due to its designing goal, schan has a separate layer of retrying and
// backup request. Namely when schan fails to access a sub channel, it may
// retry another channel. sub channels inside a schan share the information
// of accessed servers and avoid retrying accessed servers by best efforts.
// When a schan would send a backup request, it calls a sub channel with
// the request. Since a sub channel can be a combo channel as well, the
// "backup request" may be "backup requests".
//                                        ^
// CAUTION:
// =======
// Currently SelectiveChannel requires `request' to CallMethod be
// valid before the RPC ends. Other channels do not. If you're doing async
// calls with SelectiveChannel, make sure that `request' is owned and deleted
// in `done'.
class SelectiveChannel : public ChannelBase/*non-copyable*/ {
public:
    typedef SocketId ChannelHandle;

    SelectiveChannel();
    ~SelectiveChannel();

    // You MUST initialize a schan before using it. `load_balancer_name' is the
    // name of load balancing algorithm which is listed in brpc/channel.h
    // if `options' is NULL, use default options.
    int Init(const char* load_balancer_name, const ChannelOptions* options);

    // Add a sub channel, which will be deleted along with schan or explicitly
    // by RemoveAndDestroyChannel.
    // On success, handle is set with the key for removal.
    // NOTE: Different from pchan, schan can add channels at any time.
    // Returns 0 on success, -1 otherwise.
    int AddChannel(ChannelBase* sub_channel, ChannelHandle* handle);

    // Remove and destroy the sub_channel associated with `handle'.
    void RemoveAndDestroyChannel(ChannelHandle handle);

    // Send request by a sub channel. schan may retry another sub channel
    // according to retrying/backup-request settings.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    // True iff Init() was successful.
    bool initialized() const;

    void Describe(std::ostream& os, const DescribeOptions& options) const;

private:
    int CheckHealth();
    
    Channel _chan;
};

} // namespace brpc


#endif  // BRPC_SELECTIVE_CHANNEL_H
