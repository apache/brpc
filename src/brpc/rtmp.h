// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_RTMP_H
#define BRPC_RTMP_H

#include "butil/strings/string_piece.h"   // butil::StringPiece
#include "butil/endpoint.h"               // butil::EndPoint
#include "brpc/shared_object.h"          // SharedObject, intrusive_ptr
#include "brpc/socket_id.h"              // SocketUniquePtr
#include "brpc/controller.h"             // Controller, IOBuf
#include "brpc/rtmp.pb.h"                // RtmpConnectRequest
#include "brpc/amf.h"                    // AMFObject
#include "brpc/destroyable.h"            // DestroyingPtr


namespace brpc {
namespace policy {
class RtmpContext;
class RtmpChunkStream;
class OnServerStreamCreated;
}
class RtmpClientImpl;
class RtmpClientStream;
class RtmpServerStream;
class StatusService;

// ======= Audio =======

enum RtmpAudioCodec {
    RTMP_AUDIO_NONE    = 0x0001, // Raw sound, no compression
    RTMP_AUDIO_ADPCM   = 0x0002, // ADPCM compression
    RTMP_AUDIO_MP3     = 0x0004, // mp3 compression
    RTMP_AUDIO_INTEL   = 0x0008, // Not used
    RTMP_AUDIO_UNUSED  = 0x0010, // Not used
    RTMP_AUDIO_NELLY8  = 0x0020, // NellyMoser at 8-kHz compression
    RTMP_AUDIO_NELLY   = 0x0040, // NellyMoser compression (5, 11, 22, and 44 kHz)
    RTMP_AUDIO_G711A   = 0x0080, // G711A sound compression (Flash Media Server only)
    RTMP_AUDIO_G711U   = 0x0100, // G711U sound compression (Flash Media Server only)
    RTMP_AUDIO_NELLY16 = 0x0200, // NellyMouser at 16-kHz compression
    RTMP_AUDIO_AAC     = 0x0400, // Advanced audio coding (AAC) codec
    RTMP_AUDIO_SPEEX   = 0x0800, // Speex Audio
    RTMP_AUDIO_ALL     = 0x0FFF, // All RTMP-supported audio codecs
};
static const RtmpAudioCodec RTMP_AUDIO_UNKNOWN = (RtmpAudioCodec)0;

enum FlvAudioCodec {
    FLV_AUDIO_LINEAR_PCM_PLATFORM_ENDIAN = 0,
    FLV_AUDIO_ADPCM                      = 1,
    FLV_AUDIO_MP3                        = 2,
    FLV_AUDIO_LINEAR_PCM_LITTLE_ENDIAN   = 3,
    FLV_AUDIO_NELLYMOSER_16KHZ_MONO      = 4,
    FLV_AUDIO_NELLYMOSER_8KHZ_MONO       = 5,
    FLV_AUDIO_NELLYMOSER                 = 6,
    FLV_AUDIO_G711_ALAW_LOGARITHMIC_PCM  = 7,
    FLV_AUDIO_G711_MULAW_LOGARITHMIC_PCM = 8,
    FLV_AUDIO_RESERVED                   = 9,
    FLV_AUDIO_AAC                        = 10,
    FLV_AUDIO_SPEEX                      = 11,
    FLV_AUDIO_MP3_8KHZ                   = 14,
    FLV_AUDIO_DEVICE_SPECIFIC_SOUND      = 15,
};
// note: 16 is always safe because SoundFormat in flv spec is only 4 bits.
static const FlvAudioCodec FLV_AUDIO_UNKNOWN = (FlvAudioCodec)16/*note*/;

const char* FlvAudioCodec2Str(FlvAudioCodec);

enum FlvSoundRate {
    FLV_SOUND_RATE_5512HZ               = 0,
    FLV_SOUND_RATE_11025HZ              = 1,
    FLV_SOUND_RATE_22050HZ              = 2,
    FLV_SOUND_RATE_44100HZ              = 3,
};
const char* FlvSoundRate2Str(FlvSoundRate);

// Only pertains to uncompressed formats. Compressed formats always decode
// to 16 bits internally. 
enum FlvSoundBits {
    FLV_SOUND_8BIT                      = 0,
    FLV_SOUND_16BIT                     = 1,
};
const char* FlvSoundBits2Str(FlvSoundBits);

// For Nellymoser: always 0. For AAC: always 1.
enum FlvSoundType {
    FLV_SOUND_MONO                      = 0,
    FLV_SOUND_STEREO                    = 1,
};
const char* FlvSoundType2Str(FlvSoundType);

// The Audio Message in RTMP.
struct RtmpAudioMessage {
    uint32_t timestamp;
    FlvAudioCodec codec;
    FlvSoundRate rate;
    FlvSoundBits bits;
    FlvSoundType type;
    butil::IOBuf data;

    bool IsAACSequenceHeader() const;
    size_t size() const { return data.size() + 1; }
};
std::ostream& operator<<(std::ostream&, const RtmpAudioMessage&);

enum FlvAACPacketType {
    FLV_AAC_PACKET_SEQUENCE_HEADER      = 0,
    FLV_AAC_PACKET_RAW                  = 1,
};

// The Audio Message when format == FLV_AUDIO_AAC
struct RtmpAACMessage {
    uint32_t timestamp;
    FlvSoundRate rate;
    FlvSoundBits bits;
    FlvSoundType type;
    FlvAACPacketType packet_type;

    // For sequence header:  AudioSpecificConfig
    // For raw:              Raw AAC frame data
    butil::IOBuf data;

    // Create AAC message from audio message.
    butil::Status Create(const RtmpAudioMessage& msg);

    // Size of serialized message.
    size_t size() const { return data.size() + 2; }
};

// the aac object type, for RTMP sequence header
// aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 23
enum AACObjectType {
    AAC_OBJECT_MAIN = 1,
    AAC_OBJECT_LC = 2,
    AAC_OBJECT_SSR = 3,
    AAC_OBJECT_HE = 5,    // AAC HE = LC+SBR
    AAC_OBJECT_HEV2 = 29, // AAC HEv2 = LC+SBR+PS
};
static const AACObjectType AAC_OBJECT_UNKNOWN = (AACObjectType)0;

struct AudioSpecificConfig {
    AudioSpecificConfig();
    butil::Status Create(const butil::IOBuf& buf);
    butil::Status Create(const void* data, size_t len);

    AACObjectType  aac_object;
    uint8_t        aac_sample_rate;
    uint8_t        aac_channels;
};

// ======= Video =======

enum RtmpVideoCodec {
    RTMP_VIDEO_UNUSED    =  0x0001, // Obsolete value 
    RTMP_VIDEO_JPEG      =  0x0002, // Obsolete value 
    RTMP_VIDEO_SORENSON  =  0x0004, // Sorenson Flash video 
    RTMP_VIDEO_HOMEBREW  =  0x0008, // V1 screen sharing 
    RTMP_VIDEO_VP6       =  0x0010, // On2 video (Flash 8+) 
    RTMP_VIDEO_VP6ALPHA  =  0x0020, // On2 video with alpha 
    RTMP_VIDEO_HOMEBREWV =  0x0040, // Screen sharing version 2 (Flash 8+) 
    RTMP_VIDEO_H264      =  0x0080, // H264 video 
    RTMP_VIDEO_ALL       =  0x00FF, // All RTMP-supported video 
};
static const RtmpVideoCodec RTMP_VIDEO_UNKNOWN = (RtmpVideoCodec)0;

enum RtmpVideoFunction {
    // Indicates that the client can perform frame-accurate seeks.
    RTMP_VIDEO_FUNCTION_CLIENT_SEEK = 1,
};

enum FlvVideoFrameType {
    FLV_VIDEO_FRAME_KEYFRAME              = 1, // for AVC, a seekable frame
    FLV_VIDEO_FRAME_INTERFRAME            = 2, // for AVC, a non-seekable frame
    FLV_VIDEO_FRAME_DISPOSABLE_INTERFRAME = 3, // H.263 only
    FLV_VIDEO_FRAME_GENERATED_KEYFRAME    = 4, // reserved for server use only
    FLV_VIDEO_FRAME_INFOFRAME             = 5
};
const char* FlvVideoFrameType2Str(FlvVideoFrameType);

enum FlvVideoCodec {
    FLV_VIDEO_JPEG                       = 1, // currently unused
    FLV_VIDEO_SORENSON_H263              = 2,
    FLV_VIDEO_SCREEN_VIDEO               = 3,
    FLV_VIDEO_ON2_VP6                    = 4,
    FLV_VIDEO_ON2_VP6_WITH_ALPHA_CHANNEL = 5,
    FLV_VIDEO_SCREEN_VIDEO_V2            = 6,
    FLV_VIDEO_AVC                        = 7,
    FLV_VIDEO_HEVC                       = 12
};
static const FlvVideoCodec FLV_VIDEO_UNKNOWN = (FlvVideoCodec)0;

const char* FlvVideoCodec2Str(FlvVideoCodec);

// The Video Message in RTMP.
struct RtmpVideoMessage {
    uint32_t timestamp;
    FlvVideoFrameType frame_type;
    FlvVideoCodec codec;
    butil::IOBuf data;

    // True iff this message is a sequence header of AVC codec.
    bool IsAVCSequenceHeader() const;

    // True iff this message is a sequence header of HEVC(H.265) codec.
    bool IsHEVCSequenceHeader() const;
    
    // Size of serialized message
    size_t size() const { return data.size() + 1; }
};
std::ostream& operator<<(std::ostream&, const RtmpVideoMessage&);

enum FlvAVCPacketType {
    FLV_AVC_PACKET_SEQUENCE_HEADER    = 0,
    FLV_AVC_PACKET_NALU               = 1,
    // lower level NALU sequence ender is not required or supported
    FLV_AVC_PACKET_END_OF_SEQUENCE    = 2,
};

// The Video Message when codec == FLV_VIDEO_AVC
struct RtmpAVCMessage {
    uint32_t timestamp;
    FlvVideoFrameType frame_type;
    FlvAVCPacketType packet_type;
    int32_t composition_time;

    // For sequence header:  AVCDecoderConfigurationRecord
    // For NALU:             One or more NALUs
    // For end of sequence:  empty
    butil::IOBuf data;

    // Create a AVC message from a video message.
    butil::Status Create(const RtmpVideoMessage&);

    // Size of serialized message.
    size_t size() const { return data.size() + 5; }
};

// the profile for avc/h.264.
// @see Annex A Profiles and levels, H.264-AVC-ISO_IEC_14496-10.pdf, page 205.
enum AVCProfile {
    // @see ffmpeg, libavcodec/avcodec.h:2713
    AVC_PROFILE_BASELINE             = 66,
    AVC_PROFILE_CONSTRAINED_BASELINE = 578,
    AVC_PROFILE_MAIN                 = 77,
    AVC_PROFILE_EXTENDED             = 88,
    AVC_PROFILE_HIGH                 = 100,
    AVC_PROFILE_HIGH10               = 110,
    AVC_PROFILE_HIGH10_INTRA         = 2158,
    AVC_PROFILE_HIGH422              = 122,
    AVC_PROFILE_HIGH422_INTRA        = 2170,
    AVC_PROFILE_HIGH444              = 144,
    AVC_PROFILE_HIGH444_PREDICTIVE   = 244,
    AVC_PROFILE_HIGH444_INTRA        = 2192,
};
const char* AVCProfile2Str(AVCProfile);

// the level for avc/h.264.
// @see Annex A Profiles and levels, H.264-AVC-ISO_IEC_14496-10.pdf, page 207.
enum AVCLevel {
    AVC_LEVEL_1  = 10,
    AVC_LEVEL_11 = 11,
    AVC_LEVEL_12 = 12,
    AVC_LEVEL_13 = 13,
    AVC_LEVEL_2  = 20,
    AVC_LEVEL_21 = 21,
    AVC_LEVEL_22 = 22,
    AVC_LEVEL_3  = 30,
    AVC_LEVEL_31 = 31,
    AVC_LEVEL_32 = 32,
    AVC_LEVEL_4  = 40,
    AVC_LEVEL_41 = 41,
    AVC_LEVEL_5  = 50,
    AVC_LEVEL_51 = 51,
};

// Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
enum AVCNaluType {
    AVC_NALU_EMPTY = 0,
    AVC_NALU_NONIDR = 1,
    AVC_NALU_DATAPARTITIONA = 2,
    AVC_NALU_DATAPARTITIONB = 3,
    AVC_NALU_DATAPARTITIONC = 4,
    AVC_NALU_IDR = 5,
    AVC_NALU_SEI = 6,
    AVC_NALU_SPS = 7,
    AVC_NALU_PPS = 8,
    AVC_NALU_ACCESSUNITDELIMITER = 9,
    AVC_NALU_EOSEQUENCE = 10,
    AVC_NALU_EOSTREAM = 11,
    AVC_NALU_FILTERDATA = 12,
    AVC_NALU_SPSEXT = 13,
    AVC_NALU_PREFIXNALU = 14,
    AVC_NALU_SUBSETSPS = 15,
    AVC_NALU_LAYERWITHOUTPARTITION = 19,
    AVC_NALU_CODEDSLICEEXT = 20,
};

struct AVCDecoderConfigurationRecord {
    AVCDecoderConfigurationRecord();
    
    butil::Status Create(const butil::IOBuf& buf);
    butil::Status Create(const void* data, size_t len);

    int             width;
    int             height;
    AVCProfile      avc_profile; 
    AVCLevel        avc_level; 
    int8_t          length_size_minus1;
    std::vector<std::string> sps_list;
    std::vector<std::string> pps_list;

private:
    butil::Status ParseSPS(const butil::StringPiece& buf, size_t sps_length);
};
std::ostream& operator<<(std::ostream&, const AVCDecoderConfigurationRecord&);

enum AVCNaluFormat {
    AVC_NALU_FORMAT_UNKNOWN = 0,
    AVC_NALU_FORMAT_ANNEXB,
    AVC_NALU_FORMAT_IBMF,
};

// Iterate NALUs inside RtmpAVCMessage.data
class AVCNaluIterator {
public:
    AVCNaluIterator(butil::IOBuf* data, uint32_t length_size_minus1,
                    AVCNaluFormat* format_inout);
    ~AVCNaluIterator();
    void operator++();
    operator void*() const { return _data; }
    butil::IOBuf& operator*() { return _cur_nalu; }
    butil::IOBuf* operator->() { return &_cur_nalu; }
    AVCNaluType nalu_type() const { return _nalu_type; }
private:
    // `data' is mutable, improper to be copied.
    DISALLOW_COPY_AND_ASSIGN(AVCNaluIterator);
    bool next_as_annexb();
    bool next_as_ibmf();
    void set_end() { _data = NULL; }
    butil::IOBuf* _data;
    butil::IOBuf _cur_nalu;
    AVCNaluFormat* _format;
    uint32_t _length_size_minus1;
    AVCNaluType _nalu_type;
};

// ==== Meta data ====
enum RtmpObjectEncoding {
    RTMP_AMF0 = 0, // AMF0 object encoding supported by Flash 6 and later
    RTMP_AMF3 = 3, // AMF3 encoding from Flash 9 (AS3)
}; 
const char* RtmpObjectEncoding2Str(RtmpObjectEncoding);

struct RtmpMetaData {
    uint32_t timestamp;
    AMFObject data;
};

struct RtmpCuePoint {
    uint32_t timestamp;
    AMFObject data;
};

enum class FlvHeaderFlags : uint8_t {
    VIDEO = 0x01,
    AUDIO = 0x04,
    AUDIO_AND_VIDEO = 0x05,
};

struct FlvWriterOptions {
    FlvWriterOptions() = default;

    FlvHeaderFlags flv_content_type = FlvHeaderFlags::AUDIO_AND_VIDEO;
};

struct RtmpSharedObjectMessage {
    // Not implemented yet.
};

enum FlvTagType {
    FLV_TAG_AUDIO = 8,
    FLV_TAG_VIDEO = 9,
    FLV_TAG_SCRIPT_DATA = 18,
};

class FlvWriter {
public:
    // Start appending FLV tags into the buffer
    explicit FlvWriter(butil::IOBuf* buf);
    explicit FlvWriter(butil::IOBuf* buf, const FlvWriterOptions& options);
    
    // Append a video/audio/metadata/cuepoint message into the output buffer.
    butil::Status Write(const RtmpVideoMessage&);
    butil::Status Write(const RtmpAudioMessage&);
    butil::Status Write(const RtmpMetaData&);
    butil::Status Write(const RtmpCuePoint&);

private:
    butil::Status WriteScriptData(const butil::IOBuf& req_buf, uint32_t timestamp);

private:
    bool _write_header;
    butil::IOBuf* _buf;
    FlvWriterOptions _options;
};

class FlvReader {
public:
    // Start reading FLV tags from the buffer. The data read by the following 
    // Read functions would be removed from *buf.
    explicit FlvReader(butil::IOBuf* buf);

    // Get the next message type.
    // If it is a valid flv tag, butil::Status::OK() is returned and the 
    // type is written to *type. Otherwise an error would be returned,
    // leaving *type unchanged.
    // Note: If error_code of the return value is EAGAIN, the caller 
    // should wait more data and try call PeekMessageType again.
    butil::Status PeekMessageType(FlvTagType* type);

    // Read a video/audio/metadata message from the input buffer.
    // Caller should use the result of function PeekMessageType to select an
    // appropriate function, e.g., if *type is set to FLV_TAG_AUDIO in 
    // PeekMessageType, caller should call Read(RtmpAudioMessage*) subsequently.
    butil::Status Read(RtmpVideoMessage* msg);
    butil::Status Read(RtmpAudioMessage* msg);
    butil::Status Read(RtmpMetaData* object, std::string* object_name);

private:
    butil::Status ReadHeader();

private:
    bool _read_header;
    butil::IOBuf* _buf;
};

struct RtmpPlayOptions {
    // [Required] Name of the stream to play.
    // * video (FLV) files: specify the name without a file extension,
    //   example: "sample".
    // * MP3 or ID3 tags: precede the name with mp3,
    //   example: "mp3:sample".
    // * H.264/AAC files: precede the name with mp4 and specify file extension.
    //   example: "mp4:sample.m4v"
    std::string stream_name;

    // Specifies the start time in seconds.
    // * The default value -2 means the subscriber first tries to play the live
    //   stream specified in `stream_name'. If alive stream of that name is not
    //   found, it plays the recorded stream of the same name. If there is no
    //   recorded stream with that name, the subscriber waits for a new live
    //   stream with that name and plays it when available.
    // * -1: only the live stream specified in `stream_name' is played.
    // * 0 or a positive number: a recorded stream specified by `stream_name'
    //   is played beginning from the time specified by this field. If no
    //   recorded stream is found, the next item in the playlist is played.
    double start;
    
    // Specifies the duration of playback in seconds.
    // * The default value -1 means a live stream is played until it is no
    //   longer available or a recorded stream is played until it ends.
    // * A negative number other than -1: interpreted as -1.
    // * 0: plays the single frame since the time specified in `start'
    //   from the beginning of a recorded stream. The value of `start' is
    //   assumed to be equal to or greater than 0.
    // * A positive number: plays a live stream for the time period specified
    //   by this field. After that it becomes available or plays a recorded
    //   stream for the time specified by this field. If a stream ends before
    //   the time specified by `duration', playback ends when the stream ends.
    double duration;

    // Specifies whether to flush any previous playlist. 
    bool reset;

    RtmpPlayOptions();
};

enum RtmpPublishType {
    // The stream is published and the data is recorded to a new file. The file
    // is stored on the server in a subdirectory within the directory that
    // contains the server application. If the file already exists, it is
    // overwritten.
    RTMP_PUBLISH_RECORD = 1,

    // The stream is published and the data is appended to a file. If no file
    // is found, it is created.
    RTMP_PUBLISH_APPEND,

    // Live data is published without recording it in a file. 
    RTMP_PUBLISH_LIVE,
};
const char* RtmpPublishType2Str(RtmpPublishType);
bool Str2RtmpPublishType(const butil::StringPiece&, RtmpPublishType*);

// For SetPeerBandwidth
enum RtmpLimitType {
    RTMP_LIMIT_HARD = 0,
    RTMP_LIMIT_SOFT = 1,
    RTMP_LIMIT_DYNAMIC = 2
};

// The common part of RtmpClientStream and RtmpServerStream.
class RtmpStreamBase : public SharedObject 
                     , public Destroyable {
public:
    explicit RtmpStreamBase(bool is_client);

    // @Destroyable
    // For ClientStream, this function must be called to end this stream no matter 
    // Init() is called or not. Use DestroyingPtr<> which is a specialized unique_ptr 
    // to call Destroy() automatically.
    // If this stream is enclosed in intrusive_ptr<>, this method can be called
    // before/during Init(), or multiple times, because the stream is not
    // destructed yet after calling Destroy(), otherwise the behavior is
    // undefined.
    virtual void Destroy();

    // Process media messages from the peer.
    // Following methods and OnStop() on the same stream are never called
    // simultaneously.
    // NOTE: Inputs can be modified and consumed.
    virtual void OnUserData(void* msg);
    virtual void OnCuePoint(RtmpCuePoint*);
    virtual void OnMetaData(RtmpMetaData*, const butil::StringPiece&);
    virtual void OnSharedObjectMessage(RtmpSharedObjectMessage* msg);
    virtual void OnAudioMessage(RtmpAudioMessage* msg);
    virtual void OnVideoMessage(RtmpVideoMessage* msg);

    // Will be called in the same thread before any OnMetaData/OnCuePoint
    // OnSharedObjectMessage/OnAudioMessage/OnVideoMessage are called.
    virtual void OnFirstMessage();

    // Called when this stream is about to be destroyed or the underlying
    // connection is broken. This method and above methods(OnXXX) on the
    // same stream are never called simultaneously.
    virtual void OnStop();
    
    // Send media messages to the peer.
    // Returns 0 on success, -1 otherwise.
    virtual int SendCuePoint(const RtmpCuePoint&);
    virtual int SendMetaData(const RtmpMetaData&,
                             const butil::StringPiece& name = "onMetaData");
    virtual int SendSharedObjectMessage(const RtmpSharedObjectMessage& msg);
    virtual int SendAudioMessage(const RtmpAudioMessage& msg);
    virtual int SendAACMessage(const RtmpAACMessage& msg);
    virtual int SendVideoMessage(const RtmpVideoMessage& msg);
    virtual int SendAVCMessage(const RtmpAVCMessage& msg);
    // msg is owned by the caller of this function
    virtual int SendUserMessage(void* msg);

    // Send a message to the peer to make it stop. The concrete message depends
    // on implementation of the stream.
    virtual int SendStopMessage(const butil::StringPiece& error_description);

    // // Call user's procedure at server-side.
    // // request == NULL  : send AMF null as the parameter.
    // // response == NULL : response is not needed.
    // // done == NULL     : synchronous call, asynchronous otherwise.
    // void Call(Controller* cntl,
    //           const butil::StringPiece& procedure_name,
    //           const google::protobuf::Message* request,
    //           google::protobuf::Message* response,
    //           google::protobuf::Closure* done);

    // Get id of the message stream.
    uint32_t stream_id() const { return _message_stream_id; }

    // Get id of the chunk stream.
    uint32_t chunk_stream_id() const { return _chunk_stream_id; }

    // Get ip/port of peer/self
    virtual butil::EndPoint remote_side() const;
    virtual butil::EndPoint local_side() const;

    bool is_client_stream() const { return _is_client; }
    bool is_server_stream() const { return !_is_client; }

    // True iff OnStop() was called.
    bool is_stopped() const { return _stopped; }

    // When this stream is created, got from butil::gettimeofday_us().
    int64_t create_realtime_us() const { return _create_realtime_us; }
    
    bool is_paused() const { return _paused; }

    // True if OnMetaData/OnCuePoint/OnXXXMessage() was ever called.
    bool has_data_ever() const { return _has_data_ever; }

    // The underlying socket for reading/writing.
    Socket* socket() { return _rtmpsock.get(); }
    const Socket* socket() const { return _rtmpsock.get(); }

    // Returns true when the server accepted play or publish command.
    // The acquire fence makes sure the callsite seeing true must be after
    // sending play or publish command (possibly in another thread).
    bool is_server_accepted() const
    { return _is_server_accepted.load(butil::memory_order_acquire); }

    // Explicitly notify error to current stream
    virtual void SignalError();
    
protected:
friend class policy::RtmpContext;
friend class policy::RtmpChunkStream;
friend class policy::OnServerStreamCreated;
    
    virtual ~RtmpStreamBase();

    int SendMessage(uint32_t timestamp, uint8_t message_type,
                    const butil::IOBuf& body); 
    int SendControlMessage(uint8_t message_type, const void* body, size_t);

    // OnStop is mutually exclusive with OnXXXMessage, following methods
    // implement the exclusion.
    bool BeginProcessingMessage(const char* fun_name);
    void EndProcessingMessage();
    void CallOnUserData(void* data);
    void CallOnCuePoint(RtmpCuePoint*);
    void CallOnMetaData(RtmpMetaData*, const butil::StringPiece&);
    void CallOnSharedObjectMessage(RtmpSharedObjectMessage* msg);
    void CallOnAudioMessage(RtmpAudioMessage* msg);
    void CallOnVideoMessage(RtmpVideoMessage* msg);
    void CallOnStop();

    bool _is_client;
    bool _paused;   // Only used by RtmpServerStream
    bool _stopped;  // True when OnStop() was called.
    bool _processing_msg; // True when OnXXXMessage/OnMetaData/OnCuePoint are called.
    bool _has_data_ever;
    uint32_t _message_stream_id;
    uint32_t _chunk_stream_id;
    int64_t _create_realtime_us;
    SocketUniquePtr _rtmpsock;
    butil::Mutex _call_mutex;
    butil::atomic<bool> _is_server_accepted;
};

struct RtmpClientOptions {
    // Constructed with default options.
    RtmpClientOptions();
    
    // The Server application name the client is connected to.
    std::string app;
    
    // Flash Player version. It is the same string as returned by the
    // ApplicationScript getversion () function.
    std::string flashVer;

    // URL of the source SWF file making the connection. 
    std::string swfUrl;

    // URL of the Server. It has the following format:
    //   protocol://servername:port/appName/appInstance 
    std::string tcUrl;
  
    // True if proxy is being used.
    bool fpad;

    // Indicates what audio codecs the client supports.
    RtmpAudioCodec audioCodecs;

    // Indicates what video codecs are supported.
    RtmpVideoCodec videoCodecs;

    // Indicates what special video functions are supported.
    RtmpVideoFunction videoFunction;

    // URL of the web page from where the SWF file was loaded.
    std::string pageUrl;

    // =======================================================
    // Following fields are not part of on-wire RTMP data.

    // Timeout(in milliseconds) for creating a stream.
    // Default: 1000
    int32_t timeout_ms;
    
    // Timeout(in milliseconds) for creating a stream.
    // Default: 500
    int32_t connect_timeout_ms;

    // Value of SetBufferLength sent after Play.
    // Default: 1000
    uint32_t buffer_length_ms;

    // Value of SetChunkSize sent after Play.
    // Default: 60000
    uint32_t chunk_size;

    // Value of WindowAckSize sent after connect message.
    // Default: 2500000
    uint32_t window_ack_size;

    // Indicates whether to use simplified rtmp protocol or not.
    // The process of handshaking and connection will be reduced to 0 
    // RTT by client directly sending a magic number, Connect command
    // and CreateStream command to server. Server receiving this magic 
    // number should recognize it as the beginning of simplified rtmp 
    // protocol, skip regular handshaking process and change its state 
    // as if the handshaking has already completed.
    // Default: false;
    bool simplified_rtmp;
};

// Represent the communication line to one or multiple RTMP servers.
// Notice this does NOT correspond to the "NetConnection" in AS which
// only stands for one server.
class RtmpClient {
public:
    RtmpClient();
    ~RtmpClient();
    RtmpClient(const RtmpClient&);
    RtmpClient& operator=(const RtmpClient&);

    // Specify the servers to connect.
    int Init(butil::EndPoint server_addr_and_port,
             const RtmpClientOptions& options);
    int Init(const char* server_addr_and_port,
             const RtmpClientOptions& options);
    int Init(const char* server_addr, int port,
             const RtmpClientOptions& options);
    int Init(const char* naming_service_url, 
             const char* load_balancer_name,
             const RtmpClientOptions& options);

    // True if Init() was successfully called.
    bool initialized() const;

    const RtmpClientOptions& options() const;

    void swap(RtmpClient& other) { _impl.swap(other._impl); }

private:
friend class RtmpClientStream;
    butil::intrusive_ptr<RtmpClientImpl> _impl;
};

struct RtmpHashCode {
    RtmpHashCode() : _has_hash_code(false), _hash_code(0) {}
    void operator=(uint32_t hash_code) {
        _has_hash_code = true;
        _hash_code = hash_code;
    }
    operator uint32_t() const { return _hash_code; }
    bool has_been_set() const { return _has_hash_code; }
private:
    bool _has_hash_code;
    uint32_t _hash_code;
};

struct RtmpClientStreamOptions {
    // Reuse the same RTMP connection if possible.
    // Default: true;
    bool share_connection;

    // Init() blocks until play or publish is sent.
    // Default: false
    bool wait_until_play_or_publish_is_sent;

    // Max #retries for creating the stream.
    // Default: 3
    int create_stream_max_retry;

    // stream name for play command.
    std::string play_name;

    // stream name and type for publish command.
    std::string publish_name;
    RtmpPublishType publish_type; // default: RTMP_PUBLISH_LIVE

    // The hash code for consistent hashing load balancer.
    RtmpHashCode hash_code;

    RtmpClientStreamOptions();

    const std::string& stream_name() const
    { return !publish_name.empty() ? publish_name : play_name; }
};

// Represent a "NetStream" in AS. Multiple streams can be multiplexed
// into one TCP connection.
class RtmpClientStream : public RtmpStreamBase
                       , public StreamCreator
                       , public StreamUserData {
public:
    RtmpClientStream();

    void Destroy() override;

    // Create this stream on `client' according to `options'.
    // If any error occurred during initialization, OnStop() will be called.
    // If this stream is enclosed in intrusive_ptr<> and:
    // - Destroy() was called before, Init() will return immediately.
    // - Destroy() is called during creation of the stream, the process will
    //   be cancelled and OnStop() will be called soon.
    void Init(const RtmpClient* client, const RtmpClientStreamOptions& options);

    // Change bitrate.
    int Play2(const RtmpPlay2Options&);

    // Seek the offset (in milliseconds) within a media file or playlist.
    int Seek(double offset_ms);

    int Pause(bool pause_or_unpause, double offset_ms);

    // The options passed to Init()
    const RtmpClientStreamOptions& options() const { return _options; }

    // In form of "rtmp://HOST/APP/STREAM_NAME"
    std::string rtmp_url() const;

protected:
    virtual ~RtmpClientStream();

private:
friend class policy::RtmpChunkStream;
friend class policy::OnServerStreamCreated;
friend class OnClientStreamCreated;
friend class RtmpRetryingClientStream;

    int Play(const RtmpPlayOptions& opt);
    int Publish(const butil::StringPiece& name, RtmpPublishType type);

    // @StreamCreator
    StreamUserData* OnCreatingStream(SocketUniquePtr* inout, Controller* cntl) override;
    void DestroyStreamCreator(Controller* cntl) override;

    // @StreamUserData
    void DestroyStreamUserData(SocketUniquePtr& sending_sock,
                               Controller* cntl,
                               int error_code,
                               bool end_of_rpc) override;

    void OnFailedToCreateStream();
    
    static int RunOnFailed(bthread_id_t id, void* data, int);
    void OnStopInternal();

    // Called when the stream received a status message. Server may send status
    // messages back to client for publish/seek/pause etc commands.
    void OnStatus(const RtmpInfo& info);

    // The Destroy() w/o dereference _self_ref, to be called internally by
    // client stream self.
    void SignalError() override;

    butil::intrusive_ptr<RtmpClientImpl> _client_impl;
    butil::intrusive_ptr<RtmpClientStream> _self_ref;
    bthread_id_t _onfail_id;
    CallId _create_stream_rpc_id;
    bool _from_socketmap;
    bool _created_stream_with_play_or_publish;
    enum State {
        STATE_UNINITIALIZED,
        STATE_CREATING,
        STATE_CREATED,
        STATE_ERROR,
        STATE_DESTROYING,
    };
    State _state;
    butil::Mutex _state_mutex;
    RtmpClientStreamOptions _options;
};

struct RtmpRetryingClientStreamOptions : public RtmpClientStreamOptions {
    // Wait for at least so many milliseconds before next retry.
    // Default: 1000
    int retry_interval_ms;

    // >0: Retry for so many milliseconds approximately.
    //  0: Never retry.
    // -1: Infinite retries.
    // Default: -1
    int max_retry_duration_ms;

    // Retry so many times without any delay between consecutive retries.
    // (controlled by retry_interval_ms)
    // Default: 2
    int fast_retry_count;

    // Stop retrying when ALL created streams fail before playing or
    // publishing any data. "ALL" = max(fast_retry_count, 1)
    // In most scenarios, this option should be true which may stop
    // pointless retries.
    // Default: true
    bool quit_when_no_data_ever;

    RtmpRetryingClientStreamOptions();
};

// Base class for handling the messages received by a SubStream
class RtmpMessageHandler {
public:
    virtual void OnPlayable() = 0;
    virtual void OnUserData(void*) = 0;
    virtual void OnCuePoint(brpc::RtmpCuePoint* cuepoint) = 0;
    virtual void OnMetaData(brpc::RtmpMetaData* metadata, const butil::StringPiece& name) = 0;
    virtual void OnAudioMessage(brpc::RtmpAudioMessage* msg) = 0;
    virtual void OnVideoMessage(brpc::RtmpVideoMessage* msg) = 0;
    virtual void OnSharedObjectMessage(RtmpSharedObjectMessage* msg) = 0;
    virtual void OnSubStreamStop(RtmpStreamBase* sub_stream) = 0;
    virtual ~RtmpMessageHandler() {}
};

class RtmpRetryingClientStream;
// RtmpMessageHandler for RtmpRetryingClientStream
class RetryingClientMessageHandler : public RtmpMessageHandler {
public:
    RetryingClientMessageHandler(RtmpRetryingClientStream* parent);
    ~RetryingClientMessageHandler() {}

    void OnPlayable();
    void OnUserData(void*);
    void OnCuePoint(brpc::RtmpCuePoint* cuepoint);
    void OnMetaData(brpc::RtmpMetaData* metadata, const butil::StringPiece& name);
    void OnAudioMessage(brpc::RtmpAudioMessage* msg);
    void OnVideoMessage(brpc::RtmpVideoMessage* msg);
    void OnSharedObjectMessage(RtmpSharedObjectMessage* msg);
    void OnSubStreamStop(RtmpStreamBase* sub_stream);

private:
    butil::intrusive_ptr<RtmpRetryingClientStream> _parent;
};

class SubStreamCreator {
public:
    // Create a new SubStream and use *message_handler to handle messages from
    // the current SubStream. *sub_stream is set iff the creation is successful.
    // Note: message_handler is OWNED by this creator and deleted by the creator.
    virtual void NewSubStream(RtmpMessageHandler* message_handler,
                              butil::intrusive_ptr<RtmpStreamBase>* sub_stream) = 0;
    
    // Do the Initialization of sub_stream. If an error happens, sub_stream->Destroy()
    // would be called.
    // Note: sub_stream is not OWNED by the creator.
    virtual void LaunchSubStream(RtmpStreamBase* sub_stream,
                                 RtmpRetryingClientStreamOptions* options) = 0;
    virtual ~SubStreamCreator() {}
};

class RtmpRetryingClientStream : public RtmpStreamBase {
public:
    RtmpRetryingClientStream();

    // Must be called to end this stream no matter Init() is called or not.
    void Destroy();

    // Initialize this stream with the given sub_stream_creator which may create a
    // different sub stream each time.
    // NOTE: sub_stream_creator is OWNED by this stream and deleted by this stream.
    void Init(SubStreamCreator* sub_stream_creator,
              const RtmpRetryingClientStreamOptions& options);

    // @RtmpStreamBase
    // If the stream is recreated, following methods may return -1 and set
    // errno to ERTMPPUBLISHABLE for once. (so that users can be notified to
    // resend metadata or header messages).
    int SendCuePoint(const RtmpCuePoint&);
    int SendMetaData(const RtmpMetaData&,
                     const butil::StringPiece& name = "onMetaData");
    int SendSharedObjectMessage(const RtmpSharedObjectMessage& msg);
    int SendAudioMessage(const RtmpAudioMessage& msg);
    int SendAACMessage(const RtmpAACMessage& msg);
    int SendVideoMessage(const RtmpVideoMessage& msg);
    int SendAVCMessage(const RtmpAVCMessage& msg);
    butil::EndPoint remote_side() const;
    butil::EndPoint local_side() const;

    // Call this function to stop current stream. New sub stream will be
    // tried to be created later.
    void StopCurrentStream();

    // If a sub stream was created, this method will be called in the same
    // thread before any OnMetaData/OnCuePoint/OnSharedObjectMessage/OnAudioMessage/
    // OnVideoMessage are called.
    virtual void OnPlayable();

    const RtmpRetryingClientStreamOptions& options() const { return _options; }

protected:
    ~RtmpRetryingClientStream();

private:
friend class RetryingClientMessageHandler;

    void OnSubStreamStop(RtmpStreamBase* sub_stream);
    int AcquireStreamToSend(butil::intrusive_ptr<RtmpStreamBase>*);
    static void OnRecreateTimer(void* arg);
    void Recreate();
    void CallOnStopIfNeeded();
    
    butil::intrusive_ptr<RtmpStreamBase> _using_sub_stream;
    butil::intrusive_ptr<RtmpRetryingClientStream> _self_ref;
    mutable butil::Mutex _stream_mutex;
    RtmpRetryingClientStreamOptions _options;
    butil::atomic<bool> _destroying;
    butil::atomic<bool> _called_on_stop;
    bool _changed_stream;
    bool _has_timer_ever;
    bool _is_server_accepted_ever;
    int _num_fast_retries;
    int64_t _last_creation_time_us;
    int64_t _last_retry_start_time_us;
    bthread_timer_t _create_timer_id;
    // Note: RtmpClient can be efficiently copied.
    RtmpClient _client_copy;
    SubStreamCreator* _sub_stream_creator;
};

// Utility function to get components from rtmp_url which could be in forms of:
//   rtmp://HOST/APP/STREAM_NAME
//   rtmp://HOST/APP (empty stream_name)
//   rtmp://HOST     (empty app and stream_name)
//   rtmp://HOST/APP?vhost=.../STREAM_NAME  (This is how SRS put vhost in URL)
// "rtmp://" can be ignored.
// NOTE: query strings after stream_name is not removed and returned as part
// of stream_name.
void ParseRtmpURL(const butil::StringPiece& rtmp_url,
                  butil::StringPiece* host,
                  butil::StringPiece* vhost_after_app,
                  butil::StringPiece* port,
                  butil::StringPiece* app,
                  butil::StringPiece* stream_name);
void ParseRtmpHostAndPort(const butil::StringPiece& host_and_port,
                          butil::StringPiece* host,
                          butil::StringPiece* port);
butil::StringPiece RemoveQueryStrings(const butil::StringPiece& stream_name,
                                     butil::StringPiece* query_strings);
// Returns "rtmp://HOST/APP/STREAM_NAME"
std::string MakeRtmpURL(const butil::StringPiece& host,
                        const butil::StringPiece& port,
                        const butil::StringPiece& app,
                        const butil::StringPiece& stream_name);
// Returns url removed with beginning "rtmp://".
butil::StringPiece RemoveRtmpPrefix(const butil::StringPiece& url);
// Returns url removed with beginning "xxx://"
butil::StringPiece RemoveProtocolPrefix(const butil::StringPiece& url);

// Implement this class and assign an instance to ServerOption.rtmp_service
// to enable RTMP support.
class RtmpService {
public:
    virtual ~RtmpService() {}

    // Called when receiving a Pong response from `remote_side'.
    virtual void OnPingResponse(const butil::EndPoint& remote_side,
                                uint32_t ping_timestamp);

    // Called to create a server-side stream.
    virtual RtmpServerStream* NewStream(const RtmpConnectRequest&) = 0;
    
private:
friend class StatusService;
friend class policy::RtmpChunkStream;
};

// Represent the "NetStream" on server-side.
class RtmpServerStream : public RtmpStreamBase {
public:
    RtmpServerStream();
    ~RtmpServerStream();

    // Called when receiving a play request.
    // Call status->set_error() when the play request is rejected.
    // Call done->Run() when the play request is processed (either accepted
    // or rejected)
    virtual void OnPlay(const RtmpPlayOptions&,
                        butil::Status* status,
                        google::protobuf::Closure* done);
    
    // Called when receiving a publish request.
    // Call status->set_error() when the publish request is rejected.
    // Call done->Run() when the publish request is processed (either accepted
    // Returns 0 on success, -1 otherwise.
    virtual void OnPublish(const std::string& stream_name,
                           RtmpPublishType publish_type,
                           butil::Status* status,
                           google::protobuf::Closure* done);
    
    // Called when receiving a play2 request.
    virtual void OnPlay2(const RtmpPlay2Options&);

    // Called when receiving a seek request.
    // Returns 0 on success, -1 otherwise.
    virtual int OnSeek(double offset_ms);
    
    // Called when receiving a pause/unpause request.
    // Returns 0 on success, -1 otherwise.
    virtual int OnPause(bool pause_or_unpause, double offset_ms);
    
    // Called when receiving information from Rtmp client on buffer size (in
    // milliseconds) that is used to buffer any data coming over a stream.
    // This event is sent before the server starts processing the stream.
    virtual void OnSetBufferLength(uint32_t buffer_length_ms);

    // @RtmpStreamBase, sending StreamNotFound
    int SendStopMessage(const butil::StringPiece& error_description);
    void Destroy();

private:
friend class policy::RtmpContext;
friend class policy::RtmpChunkStream;
    int SendStreamDry();
    static int RunOnFailed(bthread_id_t id, void* data, int);
    void OnStopInternal();
    // Indicating the client supports multiple streams over one connection.
    bool _client_supports_stream_multiplexing;
    bool _is_publish;
    bthread_id_t _onfail_id;
};

} // namespace brpc


#endif  // BRPC_RTMP_H
