// Copyright (c) 2014 Baidu, Inc.
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
//          Jiashun Zhu(zhujiashun@baidu.com)

#ifndef BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H
#define BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H

#include "brpc/policy/http_rpc_protocol.h"   // HttpContext
#include "brpc/input_message_base.h"
#include "brpc/protocol.h"
#include "brpc/details/hpack.h"
#include "brpc/stream_creator.h"
#include "brpc/controller.h"

namespace brpc {

namespace policy {

class H2StreamContext;

class H2ParseResult {
public:
    explicit H2ParseResult(H2Error err, int stream_id)
        : _msg(NULL), _err(err), _stream_id(stream_id) {}
    explicit H2ParseResult(H2StreamContext* msg)
        : _msg(msg), _err(H2_NO_ERROR), _stream_id(0) {}
    
    // Return H2_NO_ERROR when the result is successful.
    H2Error error() const { return _err; }
    const char* error_str() const { return H2ErrorToString(_err); }
    bool is_ok() const { return error() == H2_NO_ERROR; }
    int stream_id() const { return _stream_id; }

    // definitely NULL when result is failed.
    H2StreamContext* message() const { return _msg; }
 
private:
    H2StreamContext* _msg;
    H2Error _err;
    int _stream_id;
};

inline H2ParseResult MakeH2Error(H2Error err, int stream_id)
{ return H2ParseResult(err, stream_id); }
inline H2ParseResult MakeH2Error(H2Error err)
{ return H2ParseResult(err, 0); }
inline H2ParseResult MakeH2Message(H2StreamContext* msg)
{ return H2ParseResult(msg); }

class H2Context;
class H2FrameHead;

enum H2StreamState {
    H2_STREAM_IDLE = 0,
    H2_STREAM_RESERVED_LOCAL,
    H2_STREAM_RESERVED_REMOTE,
    H2_STREAM_OPEN,
    H2_STREAM_HALF_CLOSED_LOCAL,
    H2_STREAM_HALF_CLOSED_REMOTE,
    H2_STREAM_CLOSED,
};
const char* H2StreamState2Str(H2StreamState);

class H2UnsentRequest : public SocketMessage, public StreamCreator {
public:
    static H2UnsentRequest* New(Controller* c, uint64_t correlation_id);
    void Describe(butil::IOBuf*) const;

    int AddRefManually()
    { return _nref.fetch_add(1, butil::memory_order_relaxed); }

    void RemoveRefManually() {
        if (_nref.fetch_sub(1, butil::memory_order_release) == 1) {
            butil::atomic_thread_fence(butil::memory_order_acquire);
            Destroy();
        }
    }

    // @StreamCreator
    void ReplaceSocketForStream(SocketUniquePtr* inout, Controller* cntl);
    void OnStreamCreationDone(SocketUniquePtr& sending_sock, Controller* cntl);
    void CleanupSocketForStream(Socket* prev_sock, Controller* cntl,
                                int error_code);
    
    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*);
    size_t EstimatedByteSize();
    
private:
    std::string& push(const std::string& name)
    { return (new (&_list[_size++]) HPacker::Header(name))->value; }
    
    void push(const std::string& name, const std::string& value)
    { new (&_list[_size++]) HPacker::Header(name, value); }

    H2UnsentRequest(Controller* c)
        : _nref(1)
        , _size(0)
        , _stream_id(0)
        , _cntl(c) {}
    ~H2UnsentRequest() {}
    H2UnsentRequest(const H2UnsentRequest&);
    void operator=(const H2UnsentRequest&);
    void Destroy();

private:
    butil::atomic<int> _nref;
    uint32_t _size;
    uint32_t _stream_id;
    mutable butil::Mutex _mutex;
    Controller* _cntl;
    std::unique_ptr<H2StreamContext> _sctx;
    HPacker::Header _list[0];
};

class H2UnsentResponse : public SocketMessage {
public:
    static H2UnsentResponse* New(Controller* c);
    void Destroy();
    void Describe(butil::IOBuf*) const;
    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*);
    size_t EstimatedByteSize();
    
private:
    std::string& push(const std::string& name)
    { return (new (&_list[_size++]) HPacker::Header(name))->value; }
    
    void push(const std::string& name, const std::string& value)
    { new (&_list[_size++]) HPacker::Header(name, value); }

    H2UnsentResponse(Controller* c)
        : _size(0)
        , _stream_id(c->http_request().h2_stream_id())
        , _http_response(c->release_http_response()) {
        _data.swap(c->response_attachment());
    }
    ~H2UnsentResponse() {}
    H2UnsentResponse(const H2UnsentResponse&);
    void operator=(const H2UnsentResponse&);

private:
    uint32_t _size;
    uint32_t _stream_id;
    std::unique_ptr<HttpHeader> _http_response;
    butil::IOBuf _data;
    HPacker::Header _list[0];
};

// Used in http_rpc_protocol.cpp
class H2StreamContext : public HttpContext {
public:
    H2StreamContext();
    void Init(H2Context* conn_ctx, int stream_id);

    H2StreamContext(H2Context* conn_ctx, int stream_id);
    // Decode headers in HPACK from *it and set into this->header(). The input
    // does not need to complete.
    // Returns 0 on success, -1 otherwise.
    int ConsumeHeaders(butil::IOBufBytesIterator& it);
    H2ParseResult EndRemoteStream();

    H2ParseResult OnData(butil::IOBufBytesIterator&, const H2FrameHead&,
                       uint32_t frag_size, uint8_t pad_length);
    H2ParseResult OnHeaders(butil::IOBufBytesIterator&, const H2FrameHead&,
                          uint32_t frag_size, uint8_t pad_length);
    H2ParseResult OnContinuation(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnResetStream(H2Error h2_error, const H2FrameHead&);
    
    uint64_t correlation_id() const { return _correlation_id; }
    void set_correlation_id(uint64_t cid) { _correlation_id = cid; }
    
    size_t parsed_length() const { return this->_parsed_length; }
    int stream_id() const { return header().h2_stream_id(); }

#ifdef HAS_H2_STREAM_STATE
    H2StreamState state() const { return _state; }
    void SetState(H2StreamState state);
#endif

friend class H2Context;
    H2Context* _conn_ctx;
#ifdef HAS_H2_STREAM_STATE
    H2StreamState _state;
#endif
    bool _stream_ended;
    butil::atomic<int64_t> _remote_window_size;
    uint64_t _correlation_id;
    butil::IOBuf _remaining_header_fragment;
};

StreamCreator* get_h2_global_stream_creator();

ParseResult ParseH2Message(butil::IOBuf *source, Socket *socket,
                             bool read_eof, const void *arg);
void PackH2Request(butil::IOBuf* buf,
                   SocketMessage** user_message_out,
                   uint64_t correlation_id,
                   const google::protobuf::MethodDescriptor* method,
                   Controller* controller,
                   const butil::IOBuf& request,
                   const Authenticator* auth);

class H2GlobalStreamCreator : public StreamCreator {
protected:
    void ReplaceSocketForStream(SocketUniquePtr* inout, Controller* cntl);
    void OnStreamCreationDone(SocketUniquePtr& sending_sock, Controller* cntl);
    void CleanupSocketForStream(Socket* prev_sock, Controller* cntl,
                                int error_code);
private:
    butil::Mutex _mutex;
};

}  // namespace policy

} // namespace brpc

#endif // BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H
