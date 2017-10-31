// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_PARALLEL_CHANNEL_H
#define BRPC_PARALLEL_CHANNEL_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include <vector>
#include "brpc/shared_object.h"
#include "brpc/channel.h"


namespace brpc {

// Possible values of SubCall.flag, MUST be bitwise exclusive.
static const int DELETE_REQUEST = 1;
static const int DELETE_RESPONSE = 2;
static const int SKIP_SUB_CHANNEL = 4;

// Return value of CallMapper
struct SubCall {
    SubCall(const google::protobuf::MethodDescriptor* method2,
            const google::protobuf::Message* request2,
            google::protobuf::Message* response2,
            int flags2)
        : method(method2)
        , request(request2)
        , response(response2)
        , flags(flags2)
    { }

    SubCall() : method(NULL), request(NULL), response(NULL), flags(0) { }

    // Returning this makes the call to ParallelChannel fail immediately.
    static SubCall Bad() { return SubCall(); }

    // Returning this makes the channel being skipped.
    static SubCall Skip() {
        SubCall sc;
        sc.flags = SKIP_SUB_CHANNEL;
        return sc;
    }

    // True if this object is constructed by Bad().
    bool is_bad() const {
        return request == NULL || response == NULL;
    }

    // True if this object is constructed by Skip().
    // true is_skip() implies true is_bad().
    bool is_skip() const { return flags & SKIP_SUB_CHANNEL; }
            
    const google::protobuf::MethodDescriptor* method;
    const google::protobuf::Message* request;
    google::protobuf::Message* response;
    int flags;
};

// Map calls to ParallelChannel to sub channels, which can have different
// requests and responses.
// Examples:
// 1. broadcast request and merge responses:
//   return SubCall(method, request, response->New(), DELETE_RESPONSE);
//
// 2. change sth. of the request and merge responses:
//   FooRequest* copied_req = brpc::Clone<FooRequest>(request);
//   copied_req->set_xxx(...);
//   return SubCall(method, copied_req, response->New(),
//                  DELETE_REQUEST | DELETE_RESPONSE);
//
// 3. Use sub_requests in request and put sub_responses in response.
//   if (channel_index >= request->sub_request_size()) {
//       return SubCall::Bad();
//   }
//   return SubCall(sub_method, request->sub_request(channel_index),
//                  response->add_sub_response(), 0);
class CallMapper : public SharedObject {
public:
    virtual SubCall Map(int channel_index/*starting from 0*/,
                        const google::protobuf::MethodDescriptor* method,
                        const google::protobuf::Message* request,
                        google::protobuf::Message* response) = 0;
protected:
    // Only callable by subclasses and butil::intrusive_ptr
    virtual ~CallMapper() {}
};

// Clone req_base typed `Req'.
template <typename Req> Req* Clone(const google::protobuf::Message* req_base) {
    const Req* req = dynamic_cast<const Req*>(req_base);
    Req* copied_req = req->New();
    copied_req->MergeFrom(*req);
    return copied_req;
}

// Merge sub_response into response.
class ResponseMerger : public SharedObject {
public:
    enum Result {
        // the response was merged successfully
        MERGED,
        
        // the sub_response was not merged and will be counted as one failure.
        // e.g. 10 sub channels & fail_limit=4, 3 failed before merging, 1
        // failed after merging, the rpc call will be treated as 4 sub
        // failures which reaches fail_limit, thus the call is failed.
        FAIL,

        // make the call to ParallelChannel fail.
        FAIL_ALL
    };

    ResponseMerger() { }
    virtual Result Merge(google::protobuf::Message* response,
                         const google::protobuf::Message* sub_response) = 0;
protected:
    // Only callable by subclasses and butil::intrusive_ptr
    virtual ~ResponseMerger() { }
};

struct ParallelChannelOptions {
    // [NOTE] timeout of sub channels are disabled in ParallelChannel. Control
    // deadlines of RPC by this timeout_ms or Controller.set_timeout_ms()
    // 
    // Max duration of RPC over this Channel. -1 means wait indefinitely.
    // Overridable by Controller.set_timeout_ms().
    // Default: 500 (milliseconds)
    // Maximum: 0x7fffffff (roughly 30 days)
    int32_t timeout_ms;

    // The RPC is considered to be successful if number of failed sub RPC
    // does not reach this limit. Even if the RPC is timedout or canceled,
    // as long as number of failed sub RPC does not reach this limit, the
    // RPC is still successful. RPC will stop soon (rather than waiting for
    // the timeout) when the limit is reached.
    // Default: number of sub channels, meaning that the RPC to ParallChannel
    // does not fail unless all sub RPC failed.
    int fail_limit;

    // Construct with default options.
    ParallelChannelOptions();
};

// ParallelChannel(aka "pchan") accesses all sub channels simultaneously with
// optionally modified requests (by CallMapper) and merges responses (by
// ResponseMerger) when they come back. The main purpose of pchan is to make
// parallel accesses to different partitions or sub systems much easier.
// ParallelChannel is a fully functional Channel:
//   * synchronous and asynchronous RPC.
//   * deletable immediately after an asynchronous call.
//   * cancelable call_id (cancels all sub calls).
//   * timeout.
// There's no separate retrying inside ParallelChannel. To retry, enable
// retrying of sub channels.
class ParallelChannel : public ChannelBase {
friend class Controller;
public:
    ParallelChannel() { }
    ~ParallelChannel();

    // Initialize ParallelChannel with `options'.
    // NOTE: Currently this function always returns 0.
    int Init(const ParallelChannelOptions* options);

    // Add a sub channel, which can be a ParallelChannel as well.
    // `sub_channel' will be deleted in dtor of ParallelChannel when ownership
    // is OWNS_CHANNEL.
    // A sub channel can be added multiple times. If it's added with
    // brpc::OWNS_CHANNEL, it will be deleted for only once.
    // If call_mapper is NULL:
    //  - Every sub_channel will get the same `request' to ParallelChannel
    //  - responses of sub channels are New()-ed from the `response' to
    //    ParallelChannel.
    // If response_merger is NULL:
    //  - responses of sub channels will be merged to the `response' to
    //    ParalleChannel by google::protobuf::Message::MergeFrom().
    // `call_mapper' and `response_merger' are always deleted in dtor.
    // NOTE:
    //   `call_mapper' and `response_merger' are intrusively shared, namely
    //    they have referential counters to record how many sub channels are
    //    using them. As a result, one call_mapper or response_merger can be
    //    associated to different sub channels without any problem.
    // CAUTION:
    //   AddChannel() during CallMethod() is thread-unsafe!
    // Returns 0 on success, -1 otherwise.
    int AddChannel(ChannelBase* sub_channel,
                   ChannelOwnership ownership,
                   CallMapper* call_mapper,
                   ResponseMerger* response_merger);

    // Call `method' of the remote service with `request' as input, and 
    // `response' as output. `controller' contains options and extra data.
    // If `done' is not NULL, this method returns after request was sent
    // and `done->Run()' will be called when the call finishes, otherwise
    // caller blocks until the call finishes.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    // Number of sub channels.
    size_t channel_count() const { return _chans.size(); }
    
    // Reset to the state that this channel was just constructed.
    // NOTE: fail_limit is kept.
    void Reset();

    // Minimum weight of sub channels.
    // FIXME(gejun): be minimum of top(nchan-fail_limit)
    int Weight();

    // Put description into `os'.
    void Describe(std::ostream& os, const DescribeOptions&) const;

public:
    struct SubChan {
        ChannelBase* chan;
        ChannelOwnership ownership;
        butil::intrusive_ptr<CallMapper> call_mapper;
        // ParallelChannel may be dtor before async RPC call finishes and
        // merger is shared with SubDone.
        butil::intrusive_ptr<ResponseMerger> merger;
    };
    typedef std::vector<SubChan> ChannelList;

protected:
    static void* RunDoneAndDestroy(void* arg);
    int CheckHealth();

    ParallelChannelOptions _options;
    ChannelList _chans;
};

} // namespace brpc


#endif  // BRPC_PARALLEL_CHANNEL_H
