// Copyright (c) 2016 Baidu, Inc.
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
//          Jiashun Zhu (zhujiashun@baidu.com)

#ifndef BRPC_POLICY_RTMP_PROTOCOL_H
#define BRPC_POLICY_RTMP_PROTOCOL_H

#include "butil/containers/flat_map.h"
#include "brpc/protocol.h"
#include "brpc/rtmp.h"
#include "brpc/amf.h"
#include "brpc/socket.h"


namespace brpc {

class Server;
class RtmpService;
class RtmpStreamBase;

namespace policy {

const uint32_t RTMP_DEFAULT_CHUNK_SIZE = 60000; // Copy from SRS
const uint32_t RTMP_DEFAULT_WINDOW_ACK_SIZE = 2500000; // Copy from SRS
const uint32_t RTMP_MAX_CHUNK_STREAM_ID = 65599;
const uint32_t RTMP_CHUNK_ARRAY_2ND_SIZE = 256;
const uint32_t RTMP_CHUNK_ARRAY_1ST_SIZE =
    (RTMP_MAX_CHUNK_STREAM_ID + RTMP_CHUNK_ARRAY_2ND_SIZE)
    / RTMP_CHUNK_ARRAY_2ND_SIZE;
const uint32_t RTMP_CONTROL_CHUNK_STREAM_ID = 2;
const uint32_t RTMP_CONTROL_MESSAGE_STREAM_ID = 0;

enum RtmpMessageType {
    RTMP_MESSAGE_SET_CHUNK_SIZE = 1,
    RTMP_MESSAGE_ABORT = 2,
    RTMP_MESSAGE_ACK = 3,
    RTMP_MESSAGE_USER_CONTROL = 4,
    RTMP_MESSAGE_WINDOW_ACK_SIZE = 5,
    RTMP_MESSAGE_SET_PEER_BANDWIDTH = 6,
    RTMP_MESSAGE_AUDIO = 8,
    RTMP_MESSAGE_VIDEO = 9,
    RTMP_MESSAGE_DATA_AMF3 = 15,
    RTMP_MESSAGE_SHARED_OBJECT_AMF3 = 16,
    RTMP_MESSAGE_COMMAND_AMF3 = 17,
    RTMP_MESSAGE_DATA_AMF0 = 18,
    RTMP_MESSAGE_SHARED_OBJECT_AMF0 = 19,
    RTMP_MESSAGE_COMMAND_AMF0 = 20,
    RTMP_MESSAGE_AGGREGATE = 22,
};

inline bool is_video_frame_type_valid(FlvVideoFrameType t) {
    return (t >= FLV_VIDEO_FRAME_KEYFRAME && t <= FLV_VIDEO_FRAME_INFOFRAME);
}

inline bool is_video_codec_valid(FlvVideoCodec id) {
    return (id >= FLV_VIDEO_JPEG && id <= FLV_VIDEO_HEVC);
}

// Get literal form of the message type.
const char* messagetype2str(RtmpMessageType);
const char* messagetype2str(uint8_t);

// Define string constants as macros rather than consts because macros
// are concatenatable.
#define RTMP_SIG_FMS_VER                     "3,5,3,888"
#define RTMP_SIG_CLIENT_ID                   "ASAICiss"

#define RTMP_STATUS_CODE_CONNECT_SUCCESS     "NetConnection.Connect.Success"
#define RTMP_STATUS_CODE_CONNECT_REJECTED    "NetConnection.Connect.Rejected"
#define RTMP_STATUS_CODE_PLAY_RESET          "NetStream.Play.Reset"
#define RTMP_STATUS_CODE_PLAY_START          "NetStream.Play.Start"
#define RTMP_STATUS_CODE_STREAM_NOT_FOUND    "NetStream.Play.StreamNotFound"
#define RTMP_STATUS_CODE_STREAM_PAUSE        "NetStream.Pause.Notify"
#define RTMP_STATUS_CODE_STREAM_UNPAUSE      "NetStream.Unpause.Notify"
#define RTMP_STATUS_CODE_PUBLISH_START       "NetStream.Publish.Start"
#define RTMP_STATUS_CODE_DATA_START          "NetStream.Data.Start"
#define RTMP_STATUS_CODE_UNPUBLISH_SUCCESS   "NetStream.Unpublish.Success"
#define RTMP_STATUS_CODE_STREAM_SEEK         "NetStream.Seek.Notify"

#define RTMP_AMF0_COMMAND_CONNECT            "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM      "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM       "closeStream" // SRS has this.
#define RTMP_AMF0_COMMAND_DELETE_STREAM      "deleteStream"
#define RTMP_AMF0_COMMAND_PLAY               "play"
#define RTMP_AMF0_COMMAND_PLAY2              "play2"
#define RTMP_AMF0_COMMAND_SEEK               "seek"
#define RTMP_AMF0_COMMAND_PAUSE              "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE         "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS          "onStatus"
#define RTMP_AMF0_COMMAND_RESULT             "_result"
#define RTMP_AMF0_COMMAND_ERROR              "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM     "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH         "FCPublish"
#define RTMP_AMF0_COMMAND_FC_UNPUBLISH       "FCUnpublish"
#define RTMP_AMF0_COMMAND_GET_STREAM_LENGTH  "getStreamLength"
#define RTMP_AMF0_COMMAND_CHECK_BW           "_checkbw"
#define RTMP_AMF0_COMMAND_PUBLISH            "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS         "|RtmpSampleAccess"
#define RTMP_AMF0_COMMAND_CALL               "call"
#define RTMP_AMF0_SET_DATAFRAME              "@setDataFrame"
#define RTMP_AMF0_ON_META_DATA               "onMetaData"
#define RTMP_AMF0_ON_CUE_POINT               "onCuePoint"
#define RTMP_AMF0_SAMPLE_ACCESS              "|RtmpSampleAccess"

#define RTMP_INFO_LEVEL_STATUS               "status"
#define RTMP_INFO_LEVEL_ERROR                "error"
#define RTMP_INFO_LEVEL_WARNING              "warning"

enum RtmpUserControlEventType {
    RTMP_USER_CONTROL_EVENT_STREAM_BEGIN = 0,
    RTMP_USER_CONTROL_EVENT_STREAM_EOF = 1,
    RTMP_USER_CONTROL_EVENT_STREAM_DRY = 2,
    RTMP_USER_CONTROL_EVENT_SET_BUFFER_LENGTH = 3,
    RTMP_USER_CONTROL_EVENT_STREAM_IS_RECORDED = 4,
    RTMP_USER_CONTROL_EVENT_PING_REQUEST = 6,
    RTMP_USER_CONTROL_EVENT_PING_RESPONSE = 7,

    // Not specified in any official documentation, but is sent by Flash Media
    // Server 3.5. Reference: http://repo.or.cz/w/rtmpdump.git/blob/8880d1456b282ee79979adbe7b6a6eb8ad371081:/librtmp/rtmp.c#l2787
    // After the server has sent a complete buffer, and sends a Buffer Empty
    // message, it will wait until the play duration of that buffer has passed
    // before sending a new buffer. The Buffer Ready message will be sent when
    // the new buffer starts. (There is no BufferReady message for the very
    // first buffer; presumably the Stream Begin message is sufficient for
    // that purpose.)
    RTMP_USER_CONTROL_EVENT_BUFFER_EMPTY = 31,
    RTMP_USER_CONTROL_EVENT_BUFFER_READY = 32,
};

// Header part of a RTMP message.
struct RtmpMessageHeader {
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t  message_type;
    uint32_t stream_id;

    RtmpMessageHeader()
        : timestamp(0)
        , message_length(0)
        , message_type(0)
        , stream_id(RTMP_CONTROL_MESSAGE_STREAM_ID) {
    }

    bool is_valid() const { return message_type != 0; }
};

class RtmpContext;

// The intermediate header passing to CutMessageIntoFileDescriptor.
class RtmpUnsentMessage : public SocketMessage {
public:
    RtmpMessageHeader header;
    uint32_t chunk_stream_id;
    // Set RtmpContext::_chunk_size_out to this in AppendAndDestroySelf()
    // if this field is non-zero.
    uint32_t new_chunk_size;
    butil::IOBuf body;
    // If next is not NULL, next->AppendAndDestroySelf() will be called
    // recursively. For implementing batched messages.
    SocketMessagePtr<RtmpUnsentMessage> next;
public:
    RtmpUnsentMessage()
        : chunk_stream_id(0) , new_chunk_size(0), next(NULL) {}
    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*);
};

// Notice that we can't directly pack CreateStream command in PackRtmpRequest, because 
// we need to pack an AMFObject according to ctx->can_stream_be_created_with_play_or_publish(),
// which is in the response of Connect command(sent in RtmpConnect::StartConnect).
struct RtmpCreateStreamMessage : public SocketMessage {
public:
    SocketUniquePtr socket;
    uint32_t transaction_id;
    RtmpClientStreamOptions options;
public:
    explicit RtmpCreateStreamMessage() {}
    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*);
};

enum RtmpChunkType {
    RTMP_CHUNK_TYPE0 = 0,
    RTMP_CHUNK_TYPE1 = 1,
    RTMP_CHUNK_TYPE2 = 2,
    RTMP_CHUNK_TYPE3 = 3
};

// header part of a chunk.
struct RtmpBasicHeader {
    uint32_t chunk_stream_id;
    RtmpChunkType fmt;
    uint8_t header_length;
};

// Read big-endian values from buf.
uint8_t Read1Byte(const void* buf);
uint16_t ReadBigEndian2Bytes(const void* buf);
uint32_t ReadBigEndian3Bytes(const void* buf);
uint32_t ReadBigEndian4Bytes(const void* buf);
// Write values in big-endian into *buf and forward *buf.
void Write1Byte(char** buf, uint8_t val);
void WriteBigEndian2Bytes(char** buf, uint16_t val);
void WriteBigEndian3Bytes(char** buf, uint32_t val);
void WriteBigEndian4Bytes(char** buf, uint32_t val);
void WriteLittleEndian4Bytes(char** buf, uint32_t val);

// Append the control message into `msg_buf' which is writable to Socket. 
RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, const void* body, size_t size);
RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, const butil::IOBuf& body);

// The callback associated with a transaction_id.
// If the transaction is successfully done, Run() will be called, otherwise
// Cancel() will be called.
class RtmpTransactionHandler {
public:
    virtual ~RtmpTransactionHandler() {}
    virtual void Run(bool error, const RtmpMessageHeader& mh,
                     AMFInputStream*, Socket* socket) = 0;
    virtual void Cancel() = 0;
};

class RtmpChunkStream;

// Associated with a RTMP connection.
class RtmpContext : public Destroyable {
friend class RtmpChunkStream;
friend class RtmpUnsentMessage;
public:
    // States during handshake.
    enum State {
        STATE_UNINITIALIZED,
        STATE_RECEIVED_S0S1,
        STATE_RECEIVED_S2,
        STATE_RECEIVED_C0C1,
        STATE_RECEIVED_C2,
    };

    // Get literal form of the state.
    static const char* state2str(State);

    // One of copt/service must be NULL, indicating this context belongs
    // to a server-side or client-side socket.
    RtmpContext(const RtmpClientOptions* copt, const Server* server);
    ~RtmpContext();

    // @Destroyable
    void Destroy();

    // Parse `source' from `socket'.
    // This method is only called from Protocol.Parse thus does not need
    // to be thread-safe.
    ParseResult Feed(butil::IOBuf* source, Socket* socket);

    const RtmpClientOptions* client_options() const { return _client_options; }
    const Server* server() const { return _server; }
    RtmpService* service() const { return _service; }

    bool is_server_side() const { return service() != NULL; }
    bool is_client_side() const { return service() == NULL; }

    // XXXMessageStream may be called from multiple threads(currently not),
    // so they're protected by _stream_mutex
    
    // Find the stream by its id and reference the stream with intrusive_ptr.
    // Returns true on success.
    bool FindMessageStream(uint32_t stream_id,
                           butil::intrusive_ptr<RtmpStreamBase>* stream);

    // Called in client-side to map the id to stream.
    bool AddClientStream(RtmpStreamBase* stream);

    // Called in server-side to allocate an id and map the id to stream.
    bool AddServerStream(RtmpStreamBase* stream);

    // Remove the stream from mapping.
    // Returns true on success.
    bool RemoveMessageStream(RtmpStreamBase* stream);

    // Allocate id for a transaction.
    // Returns true on success.
    // This method is called in pack_request(for createStream) where is
    // accessible by multiple threads. However creating streams is unlikely to
    // be very frequent, so this method is simply synchronized by _trans_mutex.
    bool AddTransaction(uint32_t* transaction_id,
                        RtmpTransactionHandler* handler);
    // Remove the transaction associated with the id.
    // Return the transaction handler.
    RtmpTransactionHandler* RemoveTransaction(uint32_t transaction_id);

    // Get the chunk stream by its id. The stream is created by need.
    RtmpChunkStream* GetChunkStream(uint32_t cs_id);
    // Reset the chunk stream associated with the id.
    void ClearChunkStream(uint32_t cs_id);

    // Allocate/deallocate id for a chunk stream.
    void AllocateChunkStreamId(uint32_t* chunk_stream_id);
    void DeallocateChunkStreamId(uint32_t chunk_stream_id);

    // Allocate/deallocate id for a message stream.
    bool AllocateMessageStreamId(uint32_t* message_stream_id);
    void DeallocateMessageStreamId(uint32_t message_stream_id);

    // Set the callback to be called in OnConnected(). This method should
    // be called before initiating the RTMP handshake (sending C0 and C1)
    void SetConnectCallback(void (*app_connect_done)(int, void*), void* data) {
        _on_connect = app_connect_done;
        _on_connect_arg = data;
    }
    // Called when the RTMP connection is established.
    void OnConnected(int error_code);
    bool unconnected() const { return _on_connect != NULL; }

    void only_check_simple_s0s1() { _only_check_simple_s0s1 = true; }
    bool can_stream_be_created_with_play_or_publish() const
    { return _create_stream_with_play_or_publish; }

    // Call this fn to change _state.
    void SetState(const butil::EndPoint& remote_side, State new_state);

    void set_create_stream_with_play_or_publish(bool create_stream_with_play_or_publish)
    { _create_stream_with_play_or_publish = create_stream_with_play_or_publish; }

    void set_simplified_rtmp(bool simplified_rtmp)
    { _simplified_rtmp = simplified_rtmp; }

    int SendConnectRequest(const butil::EndPoint& remote_side, int fd, bool simplified_rtmp);

private:
    ParseResult WaitForC0C1orSimpleRtmp(butil::IOBuf* source, Socket* socket);
    ParseResult WaitForC2(butil::IOBuf* source, Socket* socket);
    ParseResult WaitForS0S1(butil::IOBuf* source, Socket* socket);
    ParseResult WaitForS2(butil::IOBuf* source, Socket* socket);
    ParseResult OnChunks(butil::IOBuf* source, Socket* socket);

    // Count received bytes and send ack back if needed.
    void AddReceivedBytes(Socket* socket, uint32_t size);

private:
    State _state;
    void* _s1_digest;
    // Outbound chunksize (inbound chunksize of peer), modifiable by self.
    uint32_t _chunk_size_out;
    // Inbound chunksize(outbound chunksize of peer), modifiale by peer.
    uint32_t _chunk_size_in;
    uint32_t _window_ack_size;
    uint32_t _nonack_bytes;
    uint64_t _received_bytes;
    uint32_t _cs_id_allocator;
    std::vector<uint32_t> _free_cs_ids;
    uint32_t _ms_id_allocator;
    std::vector<uint32_t> _free_ms_ids;
    // Client-side options.
    const RtmpClientOptions* _client_options;
    // Callbacks to be called in OnConnected().
    void (*_on_connect)(int, void*);
    void* _on_connect_arg;
    bool _only_check_simple_s0s1;
    bool _create_stream_with_play_or_publish;
    
    // Server and service.
    const Server* _server;
    RtmpService* _service;
    
    // Mapping message_stream_id to message streams.
    butil::Mutex _stream_mutex;
    struct MessageStreamInfo {
        butil::intrusive_ptr<RtmpStreamBase> stream;
    };
    butil::FlatMap<uint32_t, MessageStreamInfo> _mstream_map;

    // Mapping transaction id to handlers.
    butil::Mutex _trans_mutex;
    uint32_t _trans_id_allocator;
    butil::FlatMap<uint32_t, RtmpTransactionHandler*> _trans_map;

    RtmpConnectRequest _connect_req;

    // Map chunk_stream_id to chunk streams.
    // The array is 2-level to reduce memory for most connections.
    struct SubChunkArray {
        butil::atomic<RtmpChunkStream*> ptrs[RTMP_CHUNK_ARRAY_2ND_SIZE];
        SubChunkArray();
        ~SubChunkArray();
    };
    butil::atomic<SubChunkArray*> _cstream_ctx[RTMP_CHUNK_ARRAY_1ST_SIZE];

    bool _simplified_rtmp;
};

class RtmpChunkStream {
public:
    typedef bool (RtmpChunkStream::*MessageHandler)(
        const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket);

    typedef bool (RtmpChunkStream::*CommandHandler)(
        const RtmpMessageHeader& mh, AMFInputStream*, Socket* socket);

public:
    RtmpChunkStream(RtmpContext* conn_ctx, uint32_t cs_id);
    
    ParseResult Feed(const RtmpBasicHeader& bh,
                     butil::IOBuf* source, Socket* socket);

    RtmpContext* connection_context() const { return _conn_ctx; }

    uint32_t chunk_stream_id() const { return _cs_id; }

    int SerializeMessage(butil::IOBuf* buf, const RtmpMessageHeader& mh,
                         butil::IOBuf* body);
    
    bool OnMessage(
        const RtmpBasicHeader& bh, const RtmpMessageHeader& mh,
        butil::IOBuf* msg_body, Socket* socket);

    bool OnSetChunkSize(const RtmpMessageHeader& mh,
                        butil::IOBuf* msg_body, Socket* socket);
    bool OnAbortMessage(const RtmpMessageHeader& mh,
                        butil::IOBuf* msg_body, Socket* socket);
    bool OnAck(const RtmpMessageHeader& mh,
               butil::IOBuf* msg_body, Socket* socket);
    bool OnUserControlMessage(const RtmpMessageHeader& mh,
                              butil::IOBuf* msg_body, Socket* socket);
    bool OnStreamBegin(const RtmpMessageHeader&,
                       const butil::StringPiece& event_data, Socket* socket);
    bool OnStreamEOF(const RtmpMessageHeader&,
                     const butil::StringPiece& event_data, Socket* socket);
    bool OnStreamDry(const RtmpMessageHeader&,
                     const butil::StringPiece& event_data, Socket* socket);
    bool OnSetBufferLength(const RtmpMessageHeader&,
                           const butil::StringPiece& event_data, Socket* socket);
    bool OnStreamIsRecorded(const RtmpMessageHeader&,
                            const butil::StringPiece& event_data, Socket* socket);
    bool OnPingRequest(const RtmpMessageHeader&,
                       const butil::StringPiece& event_data, Socket* socket);
    bool OnPingResponse(const RtmpMessageHeader&,
                        const butil::StringPiece& event_data, Socket* socket);
    bool OnBufferEmpty(const RtmpMessageHeader&,
                       const butil::StringPiece& event_data, Socket* socket);
    bool OnBufferReady(const RtmpMessageHeader&,
                       const butil::StringPiece& event_data, Socket* socket);
    
    bool OnWindowAckSize(const RtmpMessageHeader& mh,
                         butil::IOBuf* msg_body, Socket* socket);
    bool OnSetPeerBandwidth(const RtmpMessageHeader& mh,
                            butil::IOBuf* msg_body, Socket* socket);
    
    bool OnAudioMessage(const RtmpMessageHeader& mh,
                        butil::IOBuf* msg_body, Socket* socket);
    bool OnVideoMessage(const RtmpMessageHeader& mh,
                        butil::IOBuf* msg_body, Socket* socket);
    bool OnDataMessageAMF0(const RtmpMessageHeader& mh,
                           butil::IOBuf* msg_body, Socket* socket);
    bool OnDataMessageAMF3(const RtmpMessageHeader& mh,
                           butil::IOBuf* msg_body, Socket* socket);
    bool OnSharedObjectMessageAMF0(const RtmpMessageHeader& mh,
                                   butil::IOBuf* msg_body, Socket* socket);
    bool OnSharedObjectMessageAMF3(const RtmpMessageHeader& mh,
                                   butil::IOBuf* msg_body, Socket* socket);
    bool OnCommandMessageAMF0(const RtmpMessageHeader& mh,
                              butil::IOBuf* msg_body, Socket* socket);
    bool OnCommandMessageAMF3(const RtmpMessageHeader& mh,
                              butil::IOBuf* msg_body, Socket* socket);
    bool OnAggregateMessage(const RtmpMessageHeader& mh,
                            butil::IOBuf* msg_body, Socket* socket);

    bool OnStatus(const RtmpMessageHeader& mh, AMFInputStream* istream,
                  Socket* socket);
    bool OnConnect(const RtmpMessageHeader& mh, AMFInputStream* istream,
                   Socket* socket);
    bool OnBWDone(const RtmpMessageHeader& mh, AMFInputStream* istream,
                  Socket* socket);
    bool OnResult(const RtmpMessageHeader& mh, AMFInputStream* istream,
                   Socket* socket);
    bool OnError(const RtmpMessageHeader& mh, AMFInputStream* istream,
                   Socket* socket);
    bool OnPlay(const RtmpMessageHeader& mh, AMFInputStream* istream,
                Socket* socket);
    bool OnPlay2(const RtmpMessageHeader& mh, AMFInputStream* istream,
                 Socket* socket);
    bool OnCreateStream(const RtmpMessageHeader& mh, AMFInputStream* istream,
                        Socket* socket);
    bool OnDeleteStream(const RtmpMessageHeader& mh, AMFInputStream* istream,
                        Socket* socket);
    bool OnCloseStream(const RtmpMessageHeader& mh, AMFInputStream* istream,
                        Socket* socket);
    bool OnPublish(const RtmpMessageHeader& mh, AMFInputStream* istream,
                   Socket* socket);
    bool OnReleaseStream(const RtmpMessageHeader& mh, AMFInputStream* istream,
                         Socket* socket);
    bool OnFCPublish(const RtmpMessageHeader& mh, AMFInputStream* istream,
                     Socket* socket); 
    bool OnFCUnpublish(const RtmpMessageHeader& mh, AMFInputStream* istream,
                       Socket* socket);
    bool OnGetStreamLength(const RtmpMessageHeader& mh, AMFInputStream* istream,
                           Socket* socket);
    bool OnCheckBW(const RtmpMessageHeader& mh, AMFInputStream* istream,
                   Socket* socket);
    bool OnSeek(const RtmpMessageHeader& mh, AMFInputStream* istream,
                Socket* socket);
    bool OnPause(const RtmpMessageHeader& mh, AMFInputStream* istream,
                 Socket* socket);
    
private:
    struct ReadParams {
        ReadParams();
        bool last_has_extended_ts;
        bool first_chunk_of_message;
        uint32_t last_timestamp_delta;
        uint32_t left_message_length;
        RtmpMessageHeader last_msg_header;
        butil::IOBuf msg_body;
    };
    struct WriteParams {
        WriteParams();
        bool last_has_extended_ts;
        uint32_t last_timestamp_delta;
        RtmpMessageHeader last_msg_header;
    };

    RtmpContext* _conn_ctx;
    uint32_t _cs_id;
    ReadParams _r;
    WriteParams _w;
};

// Parse binary format of rmtp.
ParseResult ParseRtmpMessage(butil::IOBuf* source, Socket *socket, bool read_eof,
                            const void *arg);

// no-op placeholder, never be called.
void ProcessRtmpMessage(InputMessageBase* msg);

// Pack createStream message
void PackRtmpRequest(butil::IOBuf* buf,
                     SocketMessage**,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* controller,
                     const butil::IOBuf& request,
                     const Authenticator* auth);

// Serialize createStream message
void SerializeRtmpRequest(butil::IOBuf* buf,
                          Controller* cntl,
                          const google::protobuf::Message* request);

// ============== inline impl. =================
// TODO(gejun): impl. do not work for big-endian machines.
inline uint8_t Read1Byte(const void* void_buf) {
    return *(const char*)void_buf;
}
inline uint16_t ReadBigEndian2Bytes(const void* void_buf) {
    uint16_t ret = 0;
    char* p = (char*)&ret;
    const char* buf = (const char*)void_buf;
    p[1] = buf[0];
    p[0] = buf[1];
    return ret;
}
inline uint32_t ReadBigEndian3Bytes(const void* void_buf) {
    uint32_t ret = 0;
    char* p = (char*)&ret;
    const char* buf = (const char*)void_buf;
    p[3] = 0;
    p[2] = buf[0];
    p[1] = buf[1];
    p[0] = buf[2];
    return ret;
}
inline uint32_t ReadBigEndian4Bytes(const void* void_buf) {
    uint32_t ret = 0;
    char* p = (char*)&ret;
    const char* buf = (const char*)void_buf;
    p[3] = buf[0];
    p[2] = buf[1];
    p[1] = buf[2];
    p[0] = buf[3];
    return ret;
}
inline void Write1Byte(char** buf, uint8_t val) {
    char* out = *buf;
    *out= val;
    *buf = out + 1;
}
inline void WriteBigEndian2Bytes(char** buf, uint16_t val) {
    const char* p = (const char*)&val;
    char* out = *buf;
    out[0] = p[1];
    out[1] = p[0];
    *buf = out + 2;
}
inline void WriteBigEndian3Bytes(char** buf, uint32_t val) {
    const char* p = (const char*)&val;
    CHECK_EQ(p[3], 0);
    char* out = *buf;
    out[0] = p[2];
    out[1] = p[1];
    out[2] = p[0];
    *buf = out + 3;
}
inline void WriteBigEndian4Bytes(char** buf, uint32_t val) {
    const char* p = (const char*)&val;
    char* out = *buf;
    out[0] = p[3];
    out[1] = p[2];
    out[2] = p[1];
    out[3] = p[0];
    *buf = out + 4;
}
inline void WriteLittleEndian4Bytes(char** buf, uint32_t val) {
    const char* p = (const char*)&val;
    char* out = *buf;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
    *buf = out + 4;
}

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_RTMP_PROTOCOL_H
