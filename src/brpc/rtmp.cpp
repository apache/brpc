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


#include <gflags/gflags.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h> // StringOutputStream
#include "bthread/bthread.h"                      // bthread_id_xx
#include "bthread/unstable.h"                     // bthread_timer_del
#include "brpc/log.h"
#include "brpc/callback.h"                   // Closure
#include "brpc/channel.h"                    // Channel
#include "brpc/socket_map.h"                 // SocketMap
#include "brpc/socket.h"                     // Socket
#include "brpc/policy/rtmp_protocol.h"       // policy::*
#include "brpc/rtmp.h"
#include "brpc/details/rtmp_utils.h"


namespace brpc {

DEFINE_bool(rtmp_server_close_connection_on_error, true,
            "Close the client connection on play/publish errors, clients setting"
            " RtmpConnectRequest.stream_multiplexing to true are not affected"
            " by this flag");

struct RtmpBvars {
    bvar::Adder<int> client_count;
    bvar::Adder<int> client_stream_count;
    bvar::Adder<int> retrying_client_stream_count;
    bvar::Adder<int> server_stream_count;

    RtmpBvars()
        : client_count("rtmp_client_count")
        , client_stream_count("rtmp_client_stream_count")
        , retrying_client_stream_count("rtmp_retrying_client_stream_count")
        , server_stream_count("rtmp_server_stream_count") {
    }
};
inline RtmpBvars* get_rtmp_bvars() {
    return butil::get_leaky_singleton<RtmpBvars>();
}

namespace policy {
int SendC0C1(int fd, bool* is_simple_handshake);
int WriteWithoutOvercrowded(Socket*, SocketMessagePtr<>& msg);
}

FlvWriter::FlvWriter(butil::IOBuf* buf)
    : _write_header(false), _buf(buf) {
}

static char g_flv_header[9] = { 'F', 'L', 'V', 0x01, 0x05, 0, 0, 0, 0x09 };

butil::Status FlvWriter::Write(const RtmpVideoMessage& msg) {
    char buf[32];
    char* p = buf;
    if (!_write_header) {
        _write_header = true;
        memcpy(p, g_flv_header, sizeof(g_flv_header));
        p += sizeof(g_flv_header);
        policy::WriteBigEndian4Bytes(&p, 0); // PreviousTagSize0
    }
    // FLV tag
    *p++ = FLV_TAG_VIDEO;
    policy::WriteBigEndian3Bytes(&p, msg.size());
    policy::WriteBigEndian3Bytes(&p, (msg.timestamp & 0xFFFFFF));
    *p++ = (msg.timestamp >> 24) & 0xFF;
    policy::WriteBigEndian3Bytes(&p, 0); // StreamID
    // header of VIDEODATA
    *p++ = ((msg.frame_type & 0xF) << 4) | (msg.codec & 0xF);
    _buf->append(buf, p - buf);
    _buf->append(msg.data);
    // PreviousTagSize
    p = buf;
    policy::WriteBigEndian4Bytes(&p, 11 + msg.size());
    _buf->append(buf, p - buf);
    return butil::Status::OK();
}

butil::Status FlvWriter::Write(const RtmpAudioMessage& msg) {
    char buf[32];
    char* p = buf;
    if (!_write_header) {
        _write_header = true;
        memcpy(p, g_flv_header, sizeof(g_flv_header));
        p += sizeof(g_flv_header);
        policy::WriteBigEndian4Bytes(&p, 0); // PreviousTagSize0
    }
    // FLV tag
    *p++ = FLV_TAG_AUDIO;
    policy::WriteBigEndian3Bytes(&p, msg.size());
    policy::WriteBigEndian3Bytes(&p, (msg.timestamp & 0xFFFFFF));
    *p++ = (msg.timestamp >> 24) & 0xFF;
    policy::WriteBigEndian3Bytes(&p, 0); // StreamID
    // header of AUDIODATA
    *p++ = ((msg.codec & 0xF) << 4)
        | ((msg.rate & 0x3) << 2)
        | ((msg.bits & 0x1) << 1)
        | (msg.type & 0x1);
    _buf->append(buf, p - buf);
    _buf->append(msg.data);
    // PreviousTagSize
    p = buf;
    policy::WriteBigEndian4Bytes(&p, 11 + msg.size());
    _buf->append(buf, p - buf);
    return butil::Status::OK();
}

butil::Status FlvWriter::WriteScriptData(const butil::IOBuf& req_buf, uint32_t timestamp) {
    char buf[32];
    char* p = buf;
    if (!_write_header) {
        _write_header = true;
        memcpy(p, g_flv_header, sizeof(g_flv_header));
        p += sizeof(g_flv_header);
        policy::WriteBigEndian4Bytes(&p, 0); // PreviousTagSize0
    }
    // FLV tag
    *p++ = FLV_TAG_SCRIPT_DATA;
    policy::WriteBigEndian3Bytes(&p, req_buf.size());
    policy::WriteBigEndian3Bytes(&p, (timestamp & 0xFFFFFF));
    *p++ = (timestamp >> 24) & 0xFF;
    policy::WriteBigEndian3Bytes(&p, 0); // StreamID
    _buf->append(buf, p - buf);
    _buf->append(req_buf);
    // PreviousTagSize
    p = buf;
    policy::WriteBigEndian4Bytes(&p, 11 + req_buf.size());
    _buf->append(buf, p - buf);
    return butil::Status::OK();
}

butil::Status FlvWriter::Write(const RtmpCuePoint& cuepoint) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_SET_DATAFRAME, &ostream);
        WriteAMFString(RTMP_AMF0_ON_CUE_POINT, &ostream);
        WriteAMFObject(cuepoint.data, &ostream);
        if (!ostream.good()) {
            return butil::Status(EINVAL, "Fail to serialize cuepoint");
        }
    }
    return WriteScriptData(req_buf, cuepoint.timestamp);
}

butil::Status FlvWriter::Write(const RtmpMetaData& metadata) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_ON_META_DATA, &ostream);
        WriteAMFObject(metadata.data, &ostream);
        if (!ostream.good()) {
            return butil::Status(EINVAL, "Fail to serialize metadata");
        }
    }
    return WriteScriptData(req_buf, metadata.timestamp);
}

FlvReader::FlvReader(butil::IOBuf* buf)
    : _read_header(false), _buf(buf) {
}

butil::Status FlvReader::ReadHeader() {
    if (!_read_header) {
        char header_buf[sizeof(g_flv_header) + 4/* PreviousTagSize0 */];
        const char* p = (const char*)_buf->fetch(header_buf, sizeof(header_buf));
        if (p == NULL) {
            return butil::Status(EAGAIN, "Fail to read, not enough data");
        }
        if (memcmp(p, g_flv_header, 3) != 0) {
            LOG(FATAL) << "Fail to parse FLV header";
            return butil::Status(EINVAL, "Fail to parse FLV header");
        }
        _buf->pop_front(sizeof(header_buf));
        _read_header = true;
    }
    return butil::Status::OK();
}

butil::Status FlvReader::PeekMessageType(FlvTagType* type_out) {
    butil::Status st = ReadHeader();
    if (!st.ok()) {
        return st;
    }
    const char* p = (const char*)_buf->fetch1();
    if (p == NULL) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    FlvTagType type = (FlvTagType)*p;
    if (type != FLV_TAG_AUDIO && type != FLV_TAG_VIDEO &&
        type != FLV_TAG_SCRIPT_DATA) {
        return butil::Status(EINVAL, "Fail to parse FLV tag");
    }
    if (type_out) {
        *type_out = type;
    }
    return butil::Status::OK();
}

butil::Status FlvReader::Read(RtmpVideoMessage* msg) {
    char tags[11];
    const unsigned char* p = (const unsigned char*)_buf->fetch(tags, sizeof(tags));
    if (p == NULL) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    if (*p != FLV_TAG_VIDEO) {
        return butil::Status(EINVAL, "Fail to parse RtmpVideoMessage");
    }
    uint32_t msg_size = policy::ReadBigEndian3Bytes(p + 1);
    uint32_t timestamp = policy::ReadBigEndian3Bytes(p + 4);
    timestamp |= (*(p + 7) << 24);
    if (_buf->length() < 11 + msg_size + 4/*PreviousTagSize*/) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    _buf->pop_front(11);
    char first_byte = 0;
    CHECK(_buf->cut1(&first_byte));
    msg->timestamp = timestamp;
    msg->frame_type = (FlvVideoFrameType)((first_byte >> 4) & 0xF);
    msg->codec = (FlvVideoCodec)(first_byte & 0xF);
    // TODO(zhujiashun): check the validation of frame_type and codec
    _buf->cutn(&msg->data, msg_size - 1);
    _buf->pop_front(4/* PreviousTagSize0 */);

    return butil::Status::OK();
}

butil::Status FlvReader::Read(RtmpAudioMessage* msg) {
    char tags[11];
    const unsigned char* p = (const unsigned char*)_buf->fetch(tags, sizeof(tags));
    if (p == NULL) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    if (*p != FLV_TAG_AUDIO) {
        return butil::Status(EINVAL, "Fail to parse RtmpAudioMessage");
    }
    uint32_t msg_size = policy::ReadBigEndian3Bytes(p + 1);
    uint32_t timestamp = policy::ReadBigEndian3Bytes(p + 4);
    timestamp |= (*(p + 7) << 24);
    if (_buf->length() < 11 + msg_size + 4/*PreviousTagSize*/) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    _buf->pop_front(11);
    char first_byte = 0;
    CHECK(_buf->cut1(&first_byte));
    msg->timestamp = timestamp;
    msg->codec = (FlvAudioCodec)((first_byte >> 4) & 0xF);
    msg->rate = (FlvSoundRate)((first_byte >> 2) & 0x3);
    msg->bits = (FlvSoundBits)((first_byte >> 1) & 0x1);
    msg->type = (FlvSoundType)(first_byte & 0x1);
    _buf->cutn(&msg->data, msg_size - 1);
    _buf->pop_front(4/* PreviousTagSize0 */);

    return butil::Status::OK();
}

butil::Status FlvReader::Read(RtmpMetaData* msg, std::string* name) {
    char tags[11];
    const unsigned char* p = (const unsigned char*)_buf->fetch(tags, sizeof(tags));
    if (p == NULL) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    if (*p != FLV_TAG_SCRIPT_DATA) {
        return butil::Status(EINVAL, "Fail to parse RtmpScriptMessage");
    }
    uint32_t msg_size = policy::ReadBigEndian3Bytes(p + 1);
    uint32_t timestamp = policy::ReadBigEndian3Bytes(p + 4);
    timestamp |= (*(p + 7) << 24);
    if (_buf->length() < 11 + msg_size + 4/*PreviousTagSize*/) {
        return butil::Status(EAGAIN, "Fail to read, not enough data");
    }
    _buf->pop_front(11);
    butil::IOBuf req_buf;
    _buf->cutn(&req_buf, msg_size);
    _buf->pop_front(4/* PreviousTagSize0 */);
    {
        butil::IOBufAsZeroCopyInputStream zc_stream(req_buf);
        AMFInputStream istream(&zc_stream);
        if (!ReadAMFString(name, &istream)) {
            return butil::Status(EINVAL, "Fail to read AMF string");
        }
        if (!ReadAMFObject(&msg->data, &istream)) {
            return butil::Status(EINVAL, "Fail to read AMF object");
        }
    }
    msg->timestamp = timestamp;
    return butil::Status::OK();
}

const char* FlvVideoFrameType2Str(FlvVideoFrameType t) {
    switch (t) {
    case FLV_VIDEO_FRAME_KEYFRAME:              return "keyframe";
    case FLV_VIDEO_FRAME_INTERFRAME:            return "interframe";
    case FLV_VIDEO_FRAME_DISPOSABLE_INTERFRAME: return "disposable interframe";
    case FLV_VIDEO_FRAME_GENERATED_KEYFRAME:    return "generated keyframe";
    case FLV_VIDEO_FRAME_INFOFRAME:             return "info/command frame";
    } 
    return "Unknown FlvVideoFrameType";
}

const char* FlvVideoCodec2Str(FlvVideoCodec id) {
    switch (id) {
    case FLV_VIDEO_JPEG:            return "JPEG";
    case FLV_VIDEO_SORENSON_H263:   return "Sorenson H.263";
    case FLV_VIDEO_SCREEN_VIDEO:    return "Screen video";
    case FLV_VIDEO_ON2_VP6:         return "On2 VP6";
    case FLV_VIDEO_ON2_VP6_WITH_ALPHA_CHANNEL:
        return "On2 VP6 with alpha channel";
    case FLV_VIDEO_SCREEN_VIDEO_V2: return "Screen video version 2";
    case FLV_VIDEO_AVC:             return "AVC";
    case FLV_VIDEO_HEVC:            return "H.265";
    }
    return "Unknown FlvVideoCodec";
}

const char* FlvAudioCodec2Str(FlvAudioCodec codec) {
    switch (codec) {
    case FLV_AUDIO_LINEAR_PCM_PLATFORM_ENDIAN:
        return "Linear PCM, platform endian";
    case FLV_AUDIO_ADPCM: return "ADPCM";
    case FLV_AUDIO_MP3: return "MP3";
    case FLV_AUDIO_LINEAR_PCM_LITTLE_ENDIAN:
        return "Linear PCM, little endian";
    case FLV_AUDIO_NELLYMOSER_16KHZ_MONO:
        return "Nellymoser 16-kHz mono";
    case FLV_AUDIO_NELLYMOSER_8KHZ_MONO:
        return "Nellymoser 8-kHz mono";
    case FLV_AUDIO_NELLYMOSER:
        return "Nellymoser";
    case FLV_AUDIO_G711_ALAW_LOGARITHMIC_PCM:
        return "G.711 A-law logarithmic PCM";
    case FLV_AUDIO_G711_MULAW_LOGARITHMIC_PCM:
        return "G.711 mu-law logarithmic PCM";
    case FLV_AUDIO_RESERVED:
        return "reserved";
    case FLV_AUDIO_AAC: return "AAC";
    case FLV_AUDIO_SPEEX: return "Speex";
    case FLV_AUDIO_MP3_8KHZ: return "MP3 8-Khz";
    case FLV_AUDIO_DEVICE_SPECIFIC_SOUND:
        return "Device-specific sound";
    }
    return "Unknown FlvAudioCodec";
}

const char* FlvSoundRate2Str(FlvSoundRate rate) {
    switch (rate) {
    case FLV_SOUND_RATE_5512HZ:  return "5512";
    case FLV_SOUND_RATE_11025HZ: return "11025";
    case FLV_SOUND_RATE_22050HZ: return "22050";
    case FLV_SOUND_RATE_44100HZ: return "44100";
    }
    return "Unknown FlvSoundRate";
}

const char* FlvSoundBits2Str(FlvSoundBits size) {
    switch (size) {
    case FLV_SOUND_8BIT:  return "8";
    case FLV_SOUND_16BIT: return "16";
    }
    return "Unknown FlvSoundBits";
}

const char* FlvSoundType2Str(FlvSoundType t) {
    switch (t) {
    case FLV_SOUND_MONO:   return "mono";
    case FLV_SOUND_STEREO: return "stereo";
    }
    return "Unknown FlvSoundType";
}

std::ostream& operator<<(std::ostream& os, const RtmpAudioMessage& msg) {
    return os << "AudioMessage{timestamp=" << msg.timestamp
              << " codec=" << FlvAudioCodec2Str(msg.codec)
              << " rate=" << FlvSoundRate2Str(msg.rate)
              << " bits=" << FlvSoundBits2Str(msg.bits)
              << " type=" << FlvSoundType2Str(msg.type)
              << " data=" << butil::ToPrintable(msg.data) << '}';
}

std::ostream& operator<<(std::ostream& os, const RtmpVideoMessage& msg) {
    return os << "VideoMessage{timestamp=" << msg.timestamp
              << " type=" << FlvVideoFrameType2Str(msg.frame_type)
              << " codec=" << FlvVideoCodec2Str(msg.codec)
              << " data=" << butil::ToPrintable(msg.data) << '}';
}

butil::Status RtmpAACMessage::Create(const RtmpAudioMessage& msg) {
    if (msg.codec != FLV_AUDIO_AAC) {
        return butil::Status(EINVAL, "codec=%s is not AAC",
                            FlvAudioCodec2Str(msg.codec));
    }
    const uint8_t* p = (const uint8_t*)msg.data.fetch1();
    if (p == NULL) {
        return butil::Status(EINVAL, "Not enough data in AudioMessage");
    }
    if (*p > FLV_AAC_PACKET_RAW) {
        return butil::Status(EINVAL, "Invalid AAC packet_type=%d", (int)*p);
    }
    this->timestamp = msg.timestamp;
    this->rate = msg.rate;
    this->bits = msg.bits;
    this->type = msg.type;
    this->packet_type = (FlvAACPacketType)*p;
    msg.data.append_to(&data, msg.data.size() - 1, 1);
    return butil::Status::OK();
}

AudioSpecificConfig::AudioSpecificConfig()
    : aac_object(AAC_OBJECT_UNKNOWN)
    , aac_sample_rate(0)
    , aac_channels(0) {
}

butil::Status AudioSpecificConfig::Create(const butil::IOBuf& buf) {
    if (buf.size() < 2u) {
        return butil::Status(EINVAL, "data_size=%" PRIu64 " is too short",
                             (uint64_t)buf.size());
    }
    char tmpbuf[2];
    buf.copy_to(tmpbuf, arraysize(tmpbuf));
    return Create(tmpbuf, arraysize(tmpbuf));
}

butil::Status AudioSpecificConfig::Create(const void* data, size_t len) {
    if (len < 2u) {
        return butil::Status(EINVAL, "data_size=%" PRIu64 " is too short", (uint64_t)len);
    }
    uint8_t profile_ObjectType = ((const char*)data)[0];
    uint8_t samplingFrequencyIndex = ((const char*)data)[1];
    aac_channels = (samplingFrequencyIndex >> 3) & 0x0f;
    aac_sample_rate = ((profile_ObjectType << 1) & 0x0e) | ((samplingFrequencyIndex >> 7) & 0x01);
    aac_object = (AACObjectType)((profile_ObjectType >> 3) & 0x1f);
    if (aac_object == AAC_OBJECT_UNKNOWN) {
        return butil::Status(EINVAL, "Invalid object type");
    }
    return butil::Status::OK();
}

bool RtmpAudioMessage::IsAACSequenceHeader() const {
    if (codec != FLV_AUDIO_AAC) {
        return false;
    }
    const uint8_t* p = (const uint8_t*)data.fetch1();
    if (p == NULL) {
        return false;
    }
    return *p == FLV_AAC_PACKET_SEQUENCE_HEADER;
}

butil::Status RtmpAVCMessage::Create(const RtmpVideoMessage& msg) {
    if (msg.codec != FLV_VIDEO_AVC) {
        return butil::Status(EINVAL, "codec=%s is not AVC",
                            FlvVideoCodec2Str(msg.codec));
    }
    uint8_t buf[4];
    const uint8_t* p = (const uint8_t*)msg.data.fetch(buf, sizeof(buf));
    if (p == NULL) {
        return butil::Status(EINVAL, "Not enough data in VideoMessage");
    }
    if (*p > FLV_AVC_PACKET_END_OF_SEQUENCE) {
        return butil::Status(EINVAL, "Invalid AVC packet_type=%d", (int)*p);
    }
    this->timestamp = msg.timestamp;
    this->frame_type = msg.frame_type;
    this->packet_type = (FlvAVCPacketType)*p;
    this->composition_time = policy::ReadBigEndian3Bytes(p + 1);
    msg.data.append_to(&data, msg.data.size() - 4, 4);
    return butil::Status::OK();
}

bool RtmpVideoMessage::IsAVCSequenceHeader() const {
    if (codec != FLV_VIDEO_AVC || frame_type != FLV_VIDEO_FRAME_KEYFRAME) {
        return false;
    }
    const uint8_t* p = (const uint8_t*)data.fetch1();
    if (p == NULL) {
        return false;
    }
    return *p == FLV_AVC_PACKET_SEQUENCE_HEADER;
}

bool RtmpVideoMessage::IsHEVCSequenceHeader() const {
    if (codec != FLV_VIDEO_HEVC || frame_type != FLV_VIDEO_FRAME_KEYFRAME) {
        return false;
    }
    const uint8_t* p = (const uint8_t*)data.fetch1();
    if (p == NULL) {
        return false;
    }
    return *p == FLV_AVC_PACKET_SEQUENCE_HEADER;
}

const char* AVCProfile2Str(AVCProfile p) {
    switch (p) {
    case AVC_PROFILE_BASELINE: return "Baseline";
    case AVC_PROFILE_CONSTRAINED_BASELINE: return "ConstrainedBaseline";
    case AVC_PROFILE_MAIN: return "Main";
    case AVC_PROFILE_EXTENDED: return "Extended";
    case AVC_PROFILE_HIGH: return "High";
    case AVC_PROFILE_HIGH10: return "High10";
    case AVC_PROFILE_HIGH10_INTRA: return "High10Intra";
    case AVC_PROFILE_HIGH422: return "High422";
    case AVC_PROFILE_HIGH422_INTRA: return "High422Intra";
    case AVC_PROFILE_HIGH444: return "High444";
    case AVC_PROFILE_HIGH444_PREDICTIVE: return "High444Predictive";
    case AVC_PROFILE_HIGH444_INTRA: return "High444Intra";
    }
    return "Unknown";
}

AVCDecoderConfigurationRecord::AVCDecoderConfigurationRecord()
    : width(0)
    , height(0)
    , avc_profile((AVCProfile)0)
    , avc_level((AVCLevel)0)
    , length_size_minus1(-1) {
}

std::ostream& operator<<(std::ostream& os,
                         const AVCDecoderConfigurationRecord& r) {
    os << "{profile=" << AVCProfile2Str(r.avc_profile)
       << " level=" << (int)r.avc_level
       << " length_size_minus1=" << (int)r.length_size_minus1
       << " width=" << r.width
       << " height=" << r.height
       << " sps=[";
    for (size_t i = 0; i < r.sps_list.size(); ++i) {
        if (i) {
            os << ' ';
        }
        os << r.sps_list[i].size();
    }
    os << "] pps=[";
    for (size_t i = 0; i < r.pps_list.size(); ++i) {
        if (i) {
            os << ' ';
        }
        os << r.pps_list[i].size();
    }
    os << "]}";
    return os;
}

butil::Status AVCDecoderConfigurationRecord::Create(const butil::IOBuf& buf) {
    // the buf should be short generally, copy it out to continuous memory
    // to simplify parsing.
    DEFINE_SMALL_ARRAY(char, cont_buf, buf.size(), 64);
    buf.copy_to(cont_buf, buf.size());
    return Create(cont_buf, buf.size());
}

butil::Status AVCDecoderConfigurationRecord::Create(const void* data, size_t len) {
    butil::StringPiece buf((const char*)data, len);
    if (buf.size() < 6) {
        return butil::Status(EINVAL, "Length=%lu is not long enough",
                            (unsigned long)buf.size());
    }
    // skip configurationVersion at buf[0]
    avc_profile = (AVCProfile)buf[1];
    // skip profile_compatibility at buf[2]
    avc_level = (AVCLevel)buf[3];
    
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    length_size_minus1 = buf[4] & 0x03;
    if (length_size_minus1 == 2) {
        return butil::Status(EINVAL, "lengthSizeMinusOne should never be 2");
    }

    // Parsing SPS
    const int num_sps = (int)(buf[5] & 0x1f);
    buf.remove_prefix(6);
    sps_list.clear();
    sps_list.reserve(num_sps);
    for (int i = 0; i < num_sps; ++i) {
        if (buf.size() < 2) {
            return butil::Status(EINVAL, "Not enough data to decode SPS-length");
        }
        const uint16_t sps_length = policy::ReadBigEndian2Bytes(buf.data());
        if (buf.size() < 2u + sps_length) {
            return butil::Status(EINVAL, "Not enough data to decode SPS");
        }
        if (sps_length > 0) {
            butil::Status st = ParseSPS(buf.data() + 2, sps_length);
            if (!st.ok()) {
                return st;
            }
            sps_list.push_back(buf.substr(2, sps_length).as_string());
        }
        buf.remove_prefix(2 + sps_length);
    }
    // Parsing PPS
    pps_list.clear();
    if (buf.empty()) {
        return butil::Status(EINVAL, "Not enough data to decode PPS");
    }
    const int num_pps = (int)buf[0];
    buf.remove_prefix(1);
    for (int i = 0; i < num_pps; ++i) {
        if (buf.size() < 2) {
            return butil::Status(EINVAL, "Not enough data to decode PPS-length");
        }
        const uint16_t pps_length = policy::ReadBigEndian2Bytes(buf.data());
        if (buf.size() < 2u + pps_length) {
            return butil::Status(EINVAL, "Not enough data to decode PPS");
        }
        if (pps_length > 0) {
            pps_list.push_back(buf.substr(2, pps_length).as_string());
        }
        buf.remove_prefix(2 + pps_length);
    }
    return butil::Status::OK();
}

butil::Status AVCDecoderConfigurationRecord::ParseSPS(
    const butil::StringPiece& buf, size_t sps_length) {
    // for NALU, 7.3.1 NAL unit syntax
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 61.
    if (buf.empty()) {
        return butil::Status(EINVAL, "SPS is empty");
    }
    const int8_t nutv = buf[0];
    const int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return butil::Status(EINVAL, "forbidden_zero_bit shall equal 0");
    }
    // nal_ref_idc not equal to 0 specifies that the content of the NAL unit
    // contains:
    //    a sequence parameter set
    // or a picture parameter set
    // or a slice of a reference picture
    // or a slice data partition of a reference picture.
    int8_t nal_ref_idc = (nutv >> 5) & 0x03;
    if (!nal_ref_idc) {
        return butil::Status(EINVAL, "nal_ref_idc is 0");
    }
    // 7.4.1 NAL unit semantics
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 61.
    // nal_unit_type specifies the type of RBSP data structure contained in
    // the NAL unit as specified in Table 7-1.
    const AVCNaluType nal_unit_type = (AVCNaluType)(nutv & 0x1f);
    if (nal_unit_type != AVC_NALU_SPS) {
        return butil::Status(EINVAL, "nal_unit_type is not %d", (int)AVC_NALU_SPS);
    }
    // Extract the rbsp from sps.
    DEFINE_SMALL_ARRAY(char, rbsp, sps_length - 1, 64);
    buf.copy(rbsp, sps_length - 1, 1);
    size_t rbsp_len = 0;    
    for (size_t i = 1; i < sps_length; ++i) {
        // XX 00 00 03 XX, the 03 byte should be dropped.
        if (!(i >= 3 && buf[i - 2] == 0 && buf[i - 1] == 0 && buf[i] == 3)) {
            rbsp[rbsp_len++] = buf[i];
        }
    }
    // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62.
    if (rbsp_len < 3) {
        return butil::Status(EINVAL, "rbsp must be at least 3 bytes");
    }
    // Decode rbsp.
    const char* p = rbsp;
    uint8_t profile_idc = *p++;
    if (!profile_idc) {
        return butil::Status(EINVAL, "profile_idc is 0");
    }
    int8_t flags = *p++;
    if (flags & 0x03) {
        return butil::Status(EINVAL, "Invalid flags=%d", (int)flags);
    }
    uint8_t level_idc = *p++;
    if (!level_idc) {
        return butil::Status(EINVAL, "level_idc is 0");
    }
    BitStream bs(p, rbsp + rbsp_len - p);
    int32_t seq_parameter_set_id = -1;
    if (avc_nalu_read_uev(&bs, &seq_parameter_set_id) != 0) {
        return butil::Status(EINVAL, "Fail to read seq_parameter_set_id");
    }
    if (seq_parameter_set_id < 0) {
        return butil::Status(EINVAL, "Invalid seq_parameter_set_id=%d",
                            (int)seq_parameter_set_id);
    }
    int32_t chroma_format_idc = -1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
        profile_idc == 86 || profile_idc == 118 || profile_idc == 128) {
        if (avc_nalu_read_uev(&bs, &chroma_format_idc) != 0) {
            return butil::Status(EINVAL, "Fail to read chroma_format_idc");
        }
        if (chroma_format_idc == 3) {
            int8_t separate_colour_plane_flag = -1;
            if (avc_nalu_read_bit(&bs, &separate_colour_plane_flag) != 0) {
                return butil::Status(EINVAL, "Fail to read separate_colour_plane_flag");
            }
        }
        int32_t bit_depth_luma_minus8 = -1;
        if (avc_nalu_read_uev(&bs, &bit_depth_luma_minus8) != 0) {
            return butil::Status(EINVAL, "Fail to read bit_depth_luma_minus8");
        }
        int32_t bit_depth_chroma_minus8 = -1;
        if (avc_nalu_read_uev(&bs, &bit_depth_chroma_minus8) != 0) {
            return butil::Status(EINVAL, "Fail to read bit_depth_chroma_minus8");
        }
        int8_t qpprime_y_zero_transform_bypass_flag = -1;
        if (avc_nalu_read_bit(&bs, &qpprime_y_zero_transform_bypass_flag) != 0) {
            return butil::Status(EINVAL, "Fail to read qpprime_y_zero_transform_bypass_flag");
        }
        int8_t seq_scaling_matrix_present_flag = -1;
        if (avc_nalu_read_bit(&bs, &seq_scaling_matrix_present_flag) != 0) {
            return butil::Status(EINVAL, "Fail to read seq_scaling_matrix_present_flag");
        }
        if (seq_scaling_matrix_present_flag) {
            int nb_scmpfs = (chroma_format_idc != 3 ? 8 : 12);
            for (int i = 0; i < nb_scmpfs; i++) {
                int8_t seq_scaling_matrix_present_flag_i = -1;
                if (avc_nalu_read_bit(&bs, &seq_scaling_matrix_present_flag_i)) {
                    return butil::Status(EINVAL, "Fail to read seq_scaling_"
                                        "matrix_present_flag[%d]", i);
                }
                if (seq_scaling_matrix_present_flag_i) {
                    return butil::Status(EINVAL, "Invalid seq_scaling_matrix_"
                                        "present_flag[%d]=%d nb_scmpfs=%d",
                                        i, (int)seq_scaling_matrix_present_flag_i,
                                        nb_scmpfs);
                }
            }
        }
    }
    int32_t log2_max_frame_num_minus4 = -1;
    if (avc_nalu_read_uev(&bs, &log2_max_frame_num_minus4) != 0) {
        return butil::Status(EINVAL, "Fail to read log2_max_frame_num_minus4");
    }
    int32_t pic_order_cnt_type = -1;
    if (avc_nalu_read_uev(&bs, &pic_order_cnt_type) != 0) {
        return butil::Status(EINVAL, "Fail to read pic_order_cnt_type");
    }
    if (pic_order_cnt_type == 0) {
        int32_t log2_max_pic_order_cnt_lsb_minus4 = -1;
        if (avc_nalu_read_uev(&bs, &log2_max_pic_order_cnt_lsb_minus4) != 0) {
            return butil::Status(EINVAL, "Fail to read log2_max_pic_order_cnt_lsb_minus4");
        }
    } else if (pic_order_cnt_type == 1) {
        int8_t delta_pic_order_always_zero_flag = -1;
        if (avc_nalu_read_bit(&bs, &delta_pic_order_always_zero_flag) != 0) {
            return butil::Status(EINVAL, "Fail to read delta_pic_order_always_zero_flag");
        }
        int32_t offset_for_non_ref_pic = -1;
        if (avc_nalu_read_uev(&bs, &offset_for_non_ref_pic) != 0) {
            return butil::Status(EINVAL, "Fail to read offset_for_non_ref_pic");
        }
        int32_t offset_for_top_to_bottom_field = -1;
        if (avc_nalu_read_uev(&bs, &offset_for_top_to_bottom_field) != 0) {
            return butil::Status(EINVAL, "Fail to read offset_for_top_to_bottom_field");
        }
        int32_t num_ref_frames_in_pic_order_cnt_cycle = -1;
        if (avc_nalu_read_uev(&bs, &num_ref_frames_in_pic_order_cnt_cycle) != 0) {
            return butil::Status(EINVAL, "Fail to read num_ref_frames_in_pic_order_cnt_cycle");
        }
        if (num_ref_frames_in_pic_order_cnt_cycle) {
            return butil::Status(EINVAL, "Invalid num_ref_frames_in_pic_order_cnt_cycle=%d",
                                num_ref_frames_in_pic_order_cnt_cycle);
        }
    }
    int32_t max_num_ref_frames = -1;
    if (avc_nalu_read_uev(&bs, &max_num_ref_frames) != 0) {
        return butil::Status(EINVAL, "Fail to read max_num_ref_frames");
    }
    int8_t gaps_in_frame_num_value_allowed_flag = -1;
    if (avc_nalu_read_bit(&bs, &gaps_in_frame_num_value_allowed_flag) != 0) {
        return butil::Status(EINVAL, "Fail to read gaps_in_frame_num_value_allowed_flag");
    }
    int32_t pic_width_in_mbs_minus1 = -1;
    if (avc_nalu_read_uev(&bs, &pic_width_in_mbs_minus1) != 0) {
        return butil::Status(EINVAL, "Fail to read pic_width_in_mbs_minus1");
    }
    int32_t pic_height_in_map_units_minus1 = -1;
    if (avc_nalu_read_uev(&bs, &pic_height_in_map_units_minus1) != 0) {
        return butil::Status(EINVAL, "Fail to read pic_height_in_map_units_minus1");
    }
    width = (int)(pic_width_in_mbs_minus1 + 1) * 16;
    height = (int)(pic_height_in_map_units_minus1 + 1) * 16;
    return butil::Status::OK();
}

static bool find_avc_annexb_nalu_start_code(const butil::IOBuf& buf,
                                            size_t* start_code_length) {
    size_t consecutive_zero_count = 0;
    for (butil::IOBufBytesIterator it(buf); it != NULL; ++it) {
        char c = *it;
        if (c == 0) {
            ++consecutive_zero_count;
        } else if (c == 1) {
            if (consecutive_zero_count >= 2) {
                if (start_code_length) {
                    *start_code_length = consecutive_zero_count + 1;
                }
                return true;
            }
            return false;
        } else {
            return false;
        }
    }
    return false;
}

static void find_avc_annexb_nalu_stop_code(const butil::IOBuf& buf,
                                           size_t* nalu_length_out,
                                           size_t* stop_code_length) {
    size_t nalu_length = 0;
    size_t consecutive_zero_count = 0;
    for (butil::IOBufBytesIterator it(buf); it != NULL; ++it) {
        unsigned char c = (unsigned char)*it;
        if (c > 1) { // most frequent
            ++nalu_length;
            consecutive_zero_count = 0;
            continue;
        }
        if (c == 0) {
            ++consecutive_zero_count;
        } else { // c == 1
            if (consecutive_zero_count >= 2) {
                if (nalu_length_out) {
                    *nalu_length_out = nalu_length;
                }
                if (stop_code_length) {
                    *stop_code_length = consecutive_zero_count + 1;
                }
                return;
            } 
            ++nalu_length;
            consecutive_zero_count = 0;
        }
    }
    if (nalu_length_out) {
        *nalu_length_out = nalu_length + consecutive_zero_count;
    }
    if (stop_code_length) {
        *stop_code_length = 0;
    }
}

AVCNaluIterator::AVCNaluIterator(butil::IOBuf* data, uint32_t length_size_minus1,
                                 AVCNaluFormat* format)
    : _data(data)
    , _format(format)
    , _length_size_minus1(length_size_minus1)
    , _nalu_type(AVC_NALU_EMPTY) {
    if (_data) {
        ++*this;
    }
}

AVCNaluIterator::~AVCNaluIterator() {
}

void AVCNaluIterator::operator++() {
    if (*_format == AVC_NALU_FORMAT_ANNEXB) {
        if (!next_as_annexb()) {
            return set_end();
        }
    } else if (*_format == AVC_NALU_FORMAT_IBMF) {
        if (!next_as_ibmf()) {
            return set_end();
        }
    } else {
        size_t start_code_length = 0;
        if (find_avc_annexb_nalu_start_code(*_data, &start_code_length) &&
            _data->size() > start_code_length) {
            if (start_code_length > 0) {
                _data->pop_front(start_code_length);
            }
            *_format = AVC_NALU_FORMAT_ANNEXB;
            if (!next_as_annexb()) {
                return set_end();
            }
        } else if (next_as_ibmf()) {
            *_format = AVC_NALU_FORMAT_IBMF;
        } else {
            set_end();
        }
    }
}

bool AVCNaluIterator::next_as_annexb() {
    if (_data->empty()) {
        return false;
    }
    size_t nalu_length = 0;
    size_t stop_code_length = 0;
    find_avc_annexb_nalu_stop_code(*_data, &nalu_length, &stop_code_length);
    _cur_nalu.clear();
    _nalu_type = AVC_NALU_EMPTY;
    if (nalu_length) {
        _data->cutn(&_cur_nalu, nalu_length);
        const uint8_t byte0 = *(const uint8_t*)_cur_nalu.fetch1();
        _nalu_type = (AVCNaluType)(byte0 & 0x1f);
    }
    if (stop_code_length) {
        _data->pop_front(stop_code_length);
    }
    return true;
}

bool AVCNaluIterator::next_as_ibmf() {
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    CHECK_NE(_length_size_minus1, 2u);

    if (_data->empty()) {
        return false;
    }
    if (_data->size() < _length_size_minus1 + 1) {
        LOG(ERROR) << "Not enough data to decode length of NALU";
        return false;
    }
    int32_t nalu_length = 0;
    char buf[4];
    if (_length_size_minus1 == 3) {
        _data->copy_to(buf, 4);
        nalu_length = policy::ReadBigEndian4Bytes(buf);
    } else if (_length_size_minus1 == 1) {
        _data->copy_to(buf, 2);
        nalu_length = policy::ReadBigEndian2Bytes(buf);
    } else {
        _data->copy_to(buf, 1);
        nalu_length = *buf;
    }
    // maybe stream is invalid format.
    // see: https://github.com/ossrs/srs/issues/183
    if (nalu_length < 0) {
        LOG(ERROR) << "Invalid nalu_length=" << nalu_length;
        return false;
    }
    if (_data->size() < _length_size_minus1 + 1 + nalu_length) {
        LOG(ERROR) << "Not enough data to decode NALU";
        return false;
    }
    _data->pop_front(_length_size_minus1 + 1);
    _cur_nalu.clear();
    _nalu_type = AVC_NALU_EMPTY;
    if (nalu_length) {
        _data->cutn(&_cur_nalu, nalu_length);
        const uint8_t byte0 = *(const uint8_t*)_cur_nalu.fetch1();
        _nalu_type = (AVCNaluType)(byte0 & 0x1f);
    }
    return true;
}

RtmpClientOptions::RtmpClientOptions()
    : fpad(false)
    , audioCodecs((RtmpAudioCodec)3575) // Copy from SRS
    , videoCodecs((RtmpVideoCodec)252)  // Copy from SRS
    , videoFunction(RTMP_VIDEO_FUNCTION_CLIENT_SEEK)
    , timeout_ms(1000)
    , connect_timeout_ms(500)
    , buffer_length_ms(1000)
    , chunk_size(policy::RTMP_DEFAULT_CHUNK_SIZE)
    , window_ack_size(policy::RTMP_DEFAULT_WINDOW_ACK_SIZE)
    , simplified_rtmp(false) {
}

// Shared by RtmpClient and RtmpClientStream(s)
class RtmpClientImpl : public SharedObject {
friend class RtmpClientStream;
public:
    RtmpClientImpl() {
        get_rtmp_bvars()->client_count << 1;
    }
    ~RtmpClientImpl() {
        get_rtmp_bvars()->client_count << -1;
        RPC_VLOG << "Destroying RtmpClientImpl=" << this;
    }

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
    
    const RtmpClientOptions& options() const { return _connect_options; }
    SocketMap& socket_map() { return _socket_map; }

    int CreateSocket(const butil::EndPoint& pt, SocketId* id);

private:
    DISALLOW_COPY_AND_ASSIGN(RtmpClientImpl);
    int CommonInit(const RtmpClientOptions& options);
    
    Channel _chan;
    RtmpClientOptions _connect_options;
    SocketMap _socket_map;
};

class RtmpConnect : public AppConnect {
public:
    // @AppConnect
    void StartConnect(const Socket* s, void (*done)(int, void*), void* data) override;
    void StopConnect(Socket* s) override;
};

void RtmpConnect::StartConnect(
    const Socket* s, void (*done)(int, void*), void* data) {
    RPC_VLOG << "Establish rtmp-level connection on " << *s;
    policy::RtmpContext* ctx =
        static_cast<policy::RtmpContext*>(s->parsing_context());
    if (ctx == NULL) {
        LOG(FATAL) << "RtmpContext of " << *s << " is NULL";
        return done(EINVAL, data);
    }

    const RtmpClientOptions* _client_options = ctx->client_options();
    if (_client_options && _client_options->simplified_rtmp) {
        ctx->set_simplified_rtmp(true);
        if (ctx->SendConnectRequest(s->remote_side(), s->fd(), true) != 0) {
            LOG(ERROR) << s->remote_side() << ": Fail to send simple connect";
            return done(EINVAL, data);
        }
        ctx->SetState(s->remote_side(), policy::RtmpContext::STATE_RECEIVED_S2);
        ctx->set_create_stream_with_play_or_publish(true);
        return done(0, data);
    }

    // Save to callback to call when RTMP connect is done.
    ctx->SetConnectCallback(done, data);
        
    // Initiate the rtmp handshake.
    bool is_simple_handshake = false;
    if (policy::SendC0C1(s->fd(), &is_simple_handshake) != 0) {
        LOG(ERROR) << s->remote_side() << ": Fail to send C0 C1";
        return done(EINVAL, data);
    }
    if (is_simple_handshake) {
        ctx->only_check_simple_s0s1();
    }
}

void RtmpConnect::StopConnect(Socket* s) {
    policy::RtmpContext* ctx =
        static_cast<policy::RtmpContext*>(s->parsing_context());
    if (ctx == NULL) {
        LOG(FATAL) << "RtmpContext of " << *s << " is NULL";
    } else {
        ctx->OnConnected(EFAILEDSOCKET);
    }
}

class RtmpSocketCreator : public SocketCreator {
public:
    RtmpSocketCreator(const RtmpClientOptions& connect_options)
        : _connect_options(connect_options) {
    }

    int CreateSocket(const SocketOptions& opt, SocketId* id) {
        SocketOptions sock_opt = opt;
        sock_opt.app_connect = std::make_shared<RtmpConnect>();
        sock_opt.initial_parsing_context = new policy::RtmpContext(&_connect_options, NULL);
        return get_client_side_messenger()->Create(sock_opt, id);
    }
    
private:
    RtmpClientOptions _connect_options;
};

int RtmpClientImpl::CreateSocket(const butil::EndPoint& pt, SocketId* id) {
    SocketOptions sock_opt;
    sock_opt.remote_side = pt;
    sock_opt.app_connect = std::make_shared<RtmpConnect>();
    sock_opt.initial_parsing_context = new policy::RtmpContext(&_connect_options, NULL);
    return get_client_side_messenger()->Create(sock_opt, id);
}

int RtmpClientImpl::CommonInit(const RtmpClientOptions& options) {
    _connect_options = options;
    SocketMapOptions sm_options;
    sm_options.socket_creator = new RtmpSocketCreator(_connect_options);
    if (_socket_map.Init(sm_options) != 0) {
        LOG(ERROR) << "Fail to init _socket_map";
        return -1;
    }
    return 0;
}

int RtmpClientImpl::Init(butil::EndPoint server_addr_and_port,
                         const RtmpClientOptions& options) {
    if (CommonInit(options) != 0) {
        return -1;
    }
    ChannelOptions copts;
    copts.connect_timeout_ms = options.connect_timeout_ms;
    copts.timeout_ms = options.timeout_ms;
    copts.protocol = PROTOCOL_RTMP;
    return _chan.Init(server_addr_and_port, &copts);
}
int RtmpClientImpl::Init(const char* server_addr_and_port,
                         const RtmpClientOptions& options) {
    if (CommonInit(options) != 0) {
        return -1;
    }
    ChannelOptions copts;
    copts.connect_timeout_ms = options.connect_timeout_ms;
    copts.timeout_ms = options.timeout_ms;
    copts.protocol = PROTOCOL_RTMP;
    return _chan.Init(server_addr_and_port, &copts);
}
int RtmpClientImpl::Init(const char* server_addr, int port,
                         const RtmpClientOptions& options) {
    if (CommonInit(options) != 0) {
        return -1;
    }
    ChannelOptions copts;
    copts.connect_timeout_ms = options.connect_timeout_ms;
    copts.timeout_ms = options.timeout_ms;
    copts.protocol = PROTOCOL_RTMP;
    return _chan.Init(server_addr, port, &copts);
}
int RtmpClientImpl::Init(const char* naming_service_url, 
                         const char* load_balancer_name,
                         const RtmpClientOptions& options) {
    if (CommonInit(options) != 0) {
        return -1;
    }
    ChannelOptions copts;
    copts.connect_timeout_ms = options.connect_timeout_ms;
    copts.timeout_ms = options.timeout_ms;
    copts.protocol = PROTOCOL_RTMP;
    return _chan.Init(naming_service_url, load_balancer_name, &copts);
}

RtmpClient::RtmpClient() {}
RtmpClient::~RtmpClient() {}
RtmpClient::RtmpClient(const RtmpClient& rhs) : _impl(rhs._impl) {}

RtmpClient& RtmpClient::operator=(const RtmpClient& rhs) {
    _impl = rhs._impl;
    return *this;
}

const RtmpClientOptions& RtmpClient::options() const {
    if (_impl) {
        return _impl->options();
    } else {
        static RtmpClientOptions dft_opt;
        return dft_opt;
    }
}

int RtmpClient::Init(butil::EndPoint server_addr_and_port,
                     const RtmpClientOptions& options) {
    butil::intrusive_ptr<RtmpClientImpl> tmp(new (std::nothrow) RtmpClientImpl);
    if (tmp == NULL) {
        LOG(FATAL) << "Fail to new RtmpClientImpl";
        return -1;
    }
    if (tmp->Init(server_addr_and_port, options) != 0) {
        return -1;
    }
    tmp.swap(_impl);
    return 0;
}

int RtmpClient::Init(const char* server_addr_and_port,
                     const RtmpClientOptions& options) {
    butil::intrusive_ptr<RtmpClientImpl> tmp(new (std::nothrow) RtmpClientImpl);
    if (tmp == NULL) {
        LOG(FATAL) << "Fail to new RtmpClientImpl";
        return -1;
    }
    if (tmp->Init(server_addr_and_port, options) != 0) {
        return -1;
    }
    tmp.swap(_impl);
    return 0;
}

int RtmpClient::Init(const char* server_addr, int port,
                     const RtmpClientOptions& options) {
    butil::intrusive_ptr<RtmpClientImpl> tmp(new (std::nothrow) RtmpClientImpl);
    if (tmp == NULL) {
        LOG(FATAL) << "Fail to new RtmpClientImpl";
        return -1;
    }
    if (tmp->Init(server_addr, port, options) != 0) {
        return -1;
    }
    tmp.swap(_impl);
    return 0;
}

int RtmpClient::Init(const char* naming_service_url, 
                     const char* load_balancer_name,
                     const RtmpClientOptions& options) {
    butil::intrusive_ptr<RtmpClientImpl> tmp(new (std::nothrow) RtmpClientImpl);
    if (tmp == NULL) {
        LOG(FATAL) << "Fail to new RtmpClientImpl";
        return -1;
    }
    if (tmp->Init(naming_service_url, load_balancer_name, options) != 0) {
        return -1;
    }
    tmp.swap(_impl);
    return 0;
}

bool RtmpClient::initialized() const { return _impl != NULL; }

RtmpStreamBase::RtmpStreamBase(bool is_client)
    : _is_client(is_client)
    , _paused(false)
    , _stopped(false)
    , _processing_msg(false)
    , _has_data_ever(false)
    , _message_stream_id(0)
    , _chunk_stream_id(0)
    , _create_realtime_us(butil::gettimeofday_us())
    , _is_server_accepted(false) {
}

RtmpStreamBase::~RtmpStreamBase() {
}

void RtmpStreamBase::Destroy() {
    return;
}

int RtmpStreamBase::SendMessage(uint32_t timestamp,
                                uint8_t message_type,
                                const butil::IOBuf& body) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (_chunk_stream_id == 0) {
        LOG(ERROR) << "SendXXXMessage can't be called before play() is received";
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg(new policy::RtmpUnsentMessage);
    msg->header.timestamp = timestamp;
    msg->header.message_length = body.size();
    msg->header.message_type = message_type;
    msg->header.stream_id = _message_stream_id;
    msg->chunk_stream_id = _chunk_stream_id;
    msg->body = body;
    return _rtmpsock->Write(msg);
}

int RtmpStreamBase::SendControlMessage(
    uint8_t message_type, const void* body, size_t size) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg(
        policy::MakeUnsentControlMessage(message_type, body, size));
    return _rtmpsock->Write(msg);
}

int RtmpStreamBase::SendCuePoint(const RtmpCuePoint& cuepoint) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_SET_DATAFRAME, &ostream);
        WriteAMFString(RTMP_AMF0_ON_CUE_POINT, &ostream);
        WriteAMFObject(cuepoint.data, &ostream);
        if (!ostream.good()) {
            LOG(ERROR) << "Fail to serialize cuepoint";
            return -1;
        }
    }
    return SendMessage(cuepoint.timestamp, policy::RTMP_MESSAGE_DATA_AMF0, req_buf);
}

int RtmpStreamBase::SendMetaData(const RtmpMetaData& metadata,
                                 const butil::StringPiece& name) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(name, &ostream);
        WriteAMFObject(metadata.data, &ostream);
        if (!ostream.good()) {
            LOG(ERROR) << "Fail to serialize metadata";
            return -1;
        }
    }
    return SendMessage(metadata.timestamp, policy::RTMP_MESSAGE_DATA_AMF0, req_buf);
}

int RtmpStreamBase::SendSharedObjectMessage(const RtmpSharedObjectMessage&) {
    CHECK(false) << "Not supported yet";
    return -1;
}

int RtmpStreamBase::SendAudioMessage(const RtmpAudioMessage& msg) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (_chunk_stream_id == 0) {
        LOG(ERROR) << __FUNCTION__ << " can't be called before play() is received";
        errno = EPERM;
        return -1;
    }
    if (_paused) {
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg2(new policy::RtmpUnsentMessage);
    msg2->header.timestamp = msg.timestamp;
    msg2->header.message_length = msg.size();
    msg2->header.message_type = policy::RTMP_MESSAGE_AUDIO;
    msg2->header.stream_id = _message_stream_id;
    msg2->chunk_stream_id = _chunk_stream_id;
    // Make audio header.
    const char audio_head =
        ((msg.codec & 0xF) << 4)
        | ((msg.rate & 0x3) << 2)
        | ((msg.bits & 0x1) << 1)
        | (msg.type & 0x1);
    msg2->body.push_back(audio_head);
    msg2->body.append(msg.data);
    return _rtmpsock->Write(msg2);
}

int RtmpStreamBase::SendAACMessage(const RtmpAACMessage& msg) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (_chunk_stream_id == 0) {
        LOG(ERROR) << __FUNCTION__ << " can't be called before play() is received";
        errno = EPERM;
        return -1;
    }
    if (_paused) {
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg2(new policy::RtmpUnsentMessage);
    msg2->header.timestamp = msg.timestamp;
    msg2->header.message_length = msg.size();
    msg2->header.message_type = policy::RTMP_MESSAGE_AUDIO;
    msg2->header.stream_id = _message_stream_id;
    msg2->chunk_stream_id = _chunk_stream_id;
    // Make audio header.
    char aac_head[2];
    aac_head[0] = ((FLV_AUDIO_AAC & 0xF) << 4)
        | ((msg.rate & 0x3) << 2)
        | ((msg.bits & 0x1) << 1)
        | (msg.type & 0x1);
    aac_head[1] = (FlvAACPacketType)msg.packet_type;
    msg2->body.append(aac_head, sizeof(aac_head));
    msg2->body.append(msg.data);
    return _rtmpsock->Write(msg2);
}

int RtmpStreamBase::SendUserMessage(void*) {
    CHECK(false) << "You should implement your own SendUserMessage";
    return 0; 
}

int RtmpStreamBase::SendVideoMessage(const RtmpVideoMessage& msg) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (_chunk_stream_id == 0) {
        LOG(ERROR) << __FUNCTION__ << " can't be called before play() is received";
        errno = EPERM;
        return -1;
    }
    if (!policy::is_video_frame_type_valid(msg.frame_type)) {
        LOG(WARNING) << "Invalid frame_type=" << (int)msg.frame_type;
    }
    if (!policy::is_video_codec_valid(msg.codec)) {
        LOG(WARNING) << "Invalid codec=" << (int)msg.codec;
    }
    if (_paused) {
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg2(new policy::RtmpUnsentMessage);
    msg2->header.timestamp = msg.timestamp;
    msg2->header.message_length = msg.size();
    msg2->header.message_type = policy::RTMP_MESSAGE_VIDEO;
    msg2->header.stream_id = _message_stream_id;
    msg2->chunk_stream_id = _chunk_stream_id;
    // Make video header
    const char video_head = ((msg.frame_type & 0xF) << 4) | (msg.codec & 0xF);
    msg2->body.push_back(video_head);
    msg2->body.append(msg.data);
    return _rtmpsock->Write(msg2);
}

int RtmpStreamBase::SendAVCMessage(const RtmpAVCMessage& msg) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (_chunk_stream_id == 0) {
        LOG(ERROR) << __FUNCTION__ << " can't be called before play() is received";
        errno = EPERM;
        return -1;
    }
    if (!policy::is_video_frame_type_valid(msg.frame_type)) {
        LOG(WARNING) << "Invalid frame_type=" << (int)msg.frame_type;
    }
    if (_paused) {
        errno = EPERM;
        return -1;
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg2(new policy::RtmpUnsentMessage);
    msg2->header.timestamp = msg.timestamp;
    msg2->header.message_length = msg.size();
    msg2->header.message_type = policy::RTMP_MESSAGE_VIDEO;
    msg2->header.stream_id = _message_stream_id;
    msg2->chunk_stream_id = _chunk_stream_id;
    // Make video header
    char avc_head[5];
    char* p = avc_head;
    *p++ = ((msg.frame_type & 0xF) << 4) | (FLV_VIDEO_AVC & 0xF);
    *p++ = (FlvAVCPacketType)msg.packet_type;
    policy::WriteBigEndian3Bytes(&p, msg.composition_time);
    msg2->body.append(avc_head, sizeof(avc_head));
    msg2->body.append(msg.data);
    return _rtmpsock->Write(msg2);
}

int RtmpStreamBase::SendStopMessage(const butil::StringPiece&) {
    return -1;
}

const char* RtmpObjectEncoding2Str(RtmpObjectEncoding e) {
    switch (e) {
    case RTMP_AMF0: return "AMF0";
    case RTMP_AMF3: return "AMF3";
    }
    return "Unknown RtmpObjectEncoding";
}

void RtmpStreamBase::SignalError() {
    return;
}

void RtmpStreamBase::OnFirstMessage() {}

void RtmpStreamBase::OnUserData(void*) {
    LOG(INFO) << remote_side() << '[' << stream_id()
              << "] ignored UserData{}";
}

void RtmpStreamBase::OnCuePoint(RtmpCuePoint* cuepoint) {
    LOG(INFO) << remote_side() << '[' << stream_id()
              << "] ignored CuePoint{" << cuepoint->data << '}';
}

void RtmpStreamBase::OnMetaData(RtmpMetaData* metadata, const butil::StringPiece& name) {
    LOG(INFO) << remote_side() << '[' << stream_id()
              << "] ignored MetaData{" << metadata->data << '}'
              << " name{" << name << '}';
}

void RtmpStreamBase::OnSharedObjectMessage(RtmpSharedObjectMessage*) {
    LOG(ERROR) << remote_side() << '[' << stream_id()
               << "] ignored SharedObjectMessage{}";
}

void RtmpStreamBase::OnAudioMessage(RtmpAudioMessage* msg) {
    LOG(ERROR) << remote_side() << '[' << stream_id() << "] ignored " << *msg;
}

void RtmpStreamBase::OnVideoMessage(RtmpVideoMessage* msg) {
    LOG(ERROR) << remote_side() << '[' << stream_id() << "] ignored " << *msg;
}

void RtmpStreamBase::OnStop() {
    // do nothing by default
}

bool RtmpStreamBase::BeginProcessingMessage(const char* fun_name) {
    std::unique_lock<butil::Mutex> mu(_call_mutex);
    if (_stopped) {
        mu.unlock();
        LOG(ERROR) << fun_name << " is called after OnStop()";
        return false;
    }
    if (_processing_msg) {
        mu.unlock();
        LOG(ERROR) << "Impossible: Another OnXXXMessage is being called!";
        return false;
    }
    _processing_msg = true;
    if (!_has_data_ever) {
        _has_data_ever = true;
        OnFirstMessage();
    }
    return true;
}

void RtmpStreamBase::EndProcessingMessage() {
    std::unique_lock<butil::Mutex> mu(_call_mutex);
    _processing_msg = false;
    if (_stopped) {
        mu.unlock();
        return OnStop();
    }
}

void RtmpStreamBase::CallOnUserData(void* data) {
    if (BeginProcessingMessage("OnUserData()")) {
        OnUserData(data);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnCuePoint(RtmpCuePoint* obj) {
    if (BeginProcessingMessage("OnCuePoint()")) {
        OnCuePoint(obj);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnMetaData(RtmpMetaData* obj, const butil::StringPiece& name) {
    if (BeginProcessingMessage("OnMetaData()")) {
        OnMetaData(obj, name);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnSharedObjectMessage(RtmpSharedObjectMessage* msg) {
    if (BeginProcessingMessage("OnSharedObjectMessage()")) {
        OnSharedObjectMessage(msg);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnAudioMessage(RtmpAudioMessage* msg) {
    if (BeginProcessingMessage("OnAudioMessage()")) {
        OnAudioMessage(msg);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnVideoMessage(RtmpVideoMessage* msg) {
    if (BeginProcessingMessage("OnVideoMessage()")) {
        OnVideoMessage(msg);
        EndProcessingMessage();
    }
}

void RtmpStreamBase::CallOnStop() {
    {
        std::unique_lock<butil::Mutex> mu(_call_mutex);
        if (_stopped) {
            mu.unlock();
            LOG(ERROR) << "OnStop() was called more than once";
            return;
        }
        _stopped = true;
        if (_processing_msg) {
            // EndProcessingMessage() will call OnStop();
            return;
        }
    }
    OnStop();
}
 
butil::EndPoint RtmpStreamBase::remote_side() const
{ return _rtmpsock ? _rtmpsock->remote_side() : butil::EndPoint(); }

butil::EndPoint RtmpStreamBase::local_side() const
{ return _rtmpsock ? _rtmpsock->local_side() : butil::EndPoint(); }

// ============ RtmpClientStream =============

RtmpClientStream::RtmpClientStream()
    : RtmpStreamBase(true)
    , _onfail_id(INVALID_BTHREAD_ID)
    , _create_stream_rpc_id(INVALID_BTHREAD_ID)
    , _from_socketmap(true)
    , _created_stream_with_play_or_publish(false)
    , _state(STATE_UNINITIALIZED) {
    get_rtmp_bvars()->client_stream_count << 1;
    _self_ref.reset(this);
}

RtmpClientStream::~RtmpClientStream() {
    get_rtmp_bvars()->client_stream_count << -1;
}

void RtmpClientStream::Destroy() {
    bthread_id_t onfail_id = INVALID_BTHREAD_ID;
    CallId create_stream_rpc_id = INVALID_BTHREAD_ID;
    butil::intrusive_ptr<RtmpClientStream> self_ref;
    
    std::unique_lock<butil::Mutex> mu(_state_mutex);
    switch (_state) {
    case STATE_UNINITIALIZED:
        _state = STATE_DESTROYING;
        mu.unlock();
        OnStopInternal(); 
        _self_ref.swap(self_ref);
        return;
    case STATE_CREATING:
        _state = STATE_DESTROYING;
        create_stream_rpc_id = _create_stream_rpc_id;
        mu.unlock();
        _self_ref.swap(self_ref);
        StartCancel(create_stream_rpc_id);
        return;
    case STATE_CREATED:
        _state = STATE_DESTROYING;
        onfail_id = _onfail_id;
        mu.unlock();
        _self_ref.swap(self_ref);
        bthread_id_error(onfail_id, 0);
        return;
    case STATE_ERROR:
        _state = STATE_DESTROYING;
        mu.unlock();
        _self_ref.swap(self_ref);
        return;
    case STATE_DESTROYING:
        // Destroy() was already called.
        return;
    }
}

void RtmpClientStream::SignalError() {
    bthread_id_t onfail_id = INVALID_BTHREAD_ID;
    std::unique_lock<butil::Mutex> mu(_state_mutex);
    switch (_state) {
    case STATE_UNINITIALIZED:
        _state = STATE_ERROR;
        mu.unlock();
        OnStopInternal(); 
        return;
    case STATE_CREATING:
        _state = STATE_ERROR;
        mu.unlock();
        return;
    case STATE_CREATED:
        _state = STATE_ERROR;
        onfail_id = _onfail_id;
        mu.unlock();
        bthread_id_error(onfail_id, 0);
        return;
    case STATE_ERROR:
    case STATE_DESTROYING:
        // SignalError() or Destroy() was already called.
        return;
    }
}

StreamUserData* RtmpClientStream::OnCreatingStream(
    SocketUniquePtr* inout, Controller* cntl) {
    {
        std::unique_lock<butil::Mutex> mu(_state_mutex);
        if (_state == STATE_ERROR || _state == STATE_DESTROYING) {
            cntl->SetFailed(EINVAL, "Fail to replace socket for stream, _state is error or destroying");
            return NULL;
        }
    }
    SocketId esid;
    if (cntl->connection_type() == CONNECTION_TYPE_SHORT) {
        if (_client_impl->CreateSocket((*inout)->remote_side(), &esid) != 0) {
            cntl->SetFailed(EINVAL, "Fail to create RTMP socket");
            return NULL;
        }
    } else {
        if (_client_impl->socket_map().Insert(
                SocketMapKey((*inout)->remote_side()), &esid) != 0) {
            cntl->SetFailed(EINVAL, "Fail to get the RTMP socket");
            return NULL;
        }
    }
    SocketUniquePtr tmp_ptr;
    if (Socket::Address(esid, &tmp_ptr) != 0) {
        cntl->SetFailed(EFAILEDSOCKET, "Fail to address RTMP SocketId=%" PRIu64
                        " from SocketMap of RtmpClient=%p",
                        esid, _client_impl.get());
        return NULL;
    }
    RPC_VLOG << "Replace Socket For Stream, RTMP socketId=" << esid
             << ", main socketId=" << (*inout)->id();
    tmp_ptr->ShareStats(inout->get());
    inout->reset(tmp_ptr.release());
    return this;
}

int RtmpClientStream::RunOnFailed(bthread_id_t id, void* data, int) {
    butil::intrusive_ptr<RtmpClientStream> stream(
        static_cast<RtmpClientStream*>(data), false);
    CHECK(stream->_rtmpsock);
    // Must happen after NotifyOnFailed which is after all other callsites
    // to OnStopInternal().
    stream->OnStopInternal();
    bthread_id_unlock_and_destroy(id);
    return 0;
}

void RtmpClientStream::OnFailedToCreateStream() {
    {
        std::unique_lock<butil::Mutex> mu(_state_mutex);
        switch (_state) {
        case STATE_CREATING:
            _state = STATE_ERROR;
            break;
        case STATE_UNINITIALIZED:
        case STATE_CREATED:
            _state = STATE_ERROR;
            mu.unlock();
            CHECK(false) << "Impossible";
            break;
        case STATE_ERROR:
        case STATE_DESTROYING:
            break;
        }
    }
    return OnStopInternal();
}

void RtmpClientStream::DestroyStreamUserData(SocketUniquePtr& sending_sock,
                                             Controller* cntl,
                                             int /*error_code*/,
                                             bool end_of_rpc) {
    if (!end_of_rpc) {
        if (sending_sock) {
            if (_from_socketmap) {
                _client_impl->socket_map().Remove(SocketMapKey(sending_sock->remote_side()),
                        sending_sock->id());
            } else {
                sending_sock->SetFailed();  // not necessary, already failed.
            }
        }
    } else {
        // Always move sending_sock into _rtmpsock at the end of rpc.
        // - If the RPC is successful, moving sending_sock prevents it from
        //   setfailed in Controller after calling this method.
        // - If the RPC is failed, OnStopInternal() can clean up the socket_map
        //   inserted in OnCreatingStream().
        _rtmpsock.swap(sending_sock);
    }
}


void RtmpClientStream::DestroyStreamCreator(Controller* cntl) {
    if (cntl->Failed()) {
        if (_rtmpsock != NULL &&
            // ^ If sending_sock is NULL, the RPC fails before _pack_request
            // which calls AddTransaction, in another word, RemoveTransaction
            // is not needed.
            cntl->ErrorCode() != ERTMPCREATESTREAM) {
            // ^ ERTMPCREATESTREAM is triggered by receiving "_error" command,
            // RemoveTransaction should already be called.
            CHECK_LT(cntl->log_id(), (uint64_t)std::numeric_limits<uint32_t>::max());
            const uint32_t transaction_id = cntl->log_id();
            policy::RtmpContext* rtmp_ctx =
                static_cast<policy::RtmpContext*>(_rtmpsock->parsing_context());
            if (rtmp_ctx == NULL) {
                LOG(FATAL) << "RtmpContext must be created";
            } else {
                policy::RtmpTransactionHandler* handler =
                    rtmp_ctx->RemoveTransaction(transaction_id);
                if (handler) {
                    handler->Cancel();
                }
            }
        }
        return OnFailedToCreateStream();
    }

    int rc = 0;
    bthread_id_t onfail_id = INVALID_BTHREAD_ID;
    {
        std::unique_lock<butil::Mutex> mu(_state_mutex);
        switch (_state) {
        case STATE_CREATING:
            CHECK(_rtmpsock);
            rc = bthread_id_create(&onfail_id, this, RunOnFailed);
            if (rc) {
                cntl->SetFailed(ENOMEM, "Fail to create _onfail_id: %s", berror(rc));
                mu.unlock();
                return OnFailedToCreateStream();
            }
            // Add a ref for RunOnFailed.
            butil::intrusive_ptr<RtmpClientStream>(this).detach();
            _state = STATE_CREATED;
            _onfail_id = onfail_id;
            break;
        case STATE_UNINITIALIZED:
        case STATE_CREATED:
            _state = STATE_ERROR;
            mu.unlock();
            CHECK(false) << "Impossible";
            return OnStopInternal();
        case STATE_ERROR:
        case STATE_DESTROYING:
            mu.unlock();
            return OnStopInternal();
        }
    }
    if (onfail_id != INVALID_BTHREAD_ID) {
        _rtmpsock->NotifyOnFailed(onfail_id);
    }
}

void RtmpClientStream::OnStopInternal() {
    if (_rtmpsock == NULL) {
        return CallOnStop();
    }

    if (!_rtmpsock->Failed() && _chunk_stream_id != 0) {
        // SRS requires closeStream which is sent over this stream.
        butil::IOBuf req_buf1;
        {
            butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf1);
            AMFOutputStream ostream(&zc_stream);
            WriteAMFString(RTMP_AMF0_COMMAND_CLOSE_STREAM, &ostream);
            WriteAMFUint32(0, &ostream);
            WriteAMFNull(&ostream);
            CHECK(ostream.good());
        }
        SocketMessagePtr<policy::RtmpUnsentMessage> msg1(new policy::RtmpUnsentMessage);
        msg1->header.message_length = req_buf1.size();
        msg1->header.message_type = policy::RTMP_MESSAGE_COMMAND_AMF0;
        msg1->header.stream_id = _message_stream_id;
        msg1->chunk_stream_id = _chunk_stream_id;
        msg1->body = req_buf1;
    
        // Send deleteStream over the control stream.
        butil::IOBuf req_buf2;
        {
            butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf2);
            AMFOutputStream ostream(&zc_stream);
            WriteAMFString(RTMP_AMF0_COMMAND_DELETE_STREAM, &ostream);
            WriteAMFUint32(0, &ostream);
            WriteAMFNull(&ostream);
            WriteAMFUint32(_message_stream_id, &ostream);
            CHECK(ostream.good());
        }
        policy::RtmpUnsentMessage* msg2 = policy::MakeUnsentControlMessage(
            policy::RTMP_MESSAGE_COMMAND_AMF0, req_buf2);
        msg1->next.reset(msg2);

        if (policy::WriteWithoutOvercrowded(_rtmpsock.get(), msg1) != 0) {
            if (errno != EFAILEDSOCKET) {
                PLOG(WARNING) << "Fail to send closeStream/deleteStream to "
                              << _rtmpsock->remote_side() << "["
                              << _message_stream_id << "]";
                // Close the connection to make sure the server-side knows the
                // closing event, however this may terminate other streams over
                // the connection as well.
                _rtmpsock->SetFailed(EFAILEDSOCKET, "Fail to send closeStream/deleteStream");
            }
        }
    }
    policy::RtmpContext* ctx =
        static_cast<policy::RtmpContext*>(_rtmpsock->parsing_context());
    if (ctx != NULL) {
        if (!ctx->RemoveMessageStream(this)) {
            // The stream is not registered yet. Is this normal?
            LOG(ERROR) << "Fail to remove stream_id=" << _message_stream_id;
        }
    } else {
        LOG(FATAL) << "RtmpContext of " << *_rtmpsock << " is NULL";
    }
    if (_from_socketmap) {
        _client_impl->socket_map().Remove(SocketMapKey(_rtmpsock->remote_side()),
                                          _rtmpsock->id());
    } else {
        _rtmpsock->ReleaseAdditionalReference();
    }
    CallOnStop();
}

RtmpPlayOptions::RtmpPlayOptions()
    : start(-2)
    , duration(-1)
    , reset(true) {
}

int RtmpClientStream::Play(const RtmpPlayOptions& opt) {
    if (_rtmpsock == NULL) {
        errno = EPERM;
        return -1;
    }
    if (opt.stream_name.empty()) {
        LOG(ERROR) << "Empty stream_name";
        errno = EINVAL;
        return -1;
    }
    if (_client_impl == NULL) {
        LOG(ERROR) << "The client stream is not created yet";
        errno = EPERM;
        return -1;
    }
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_PLAY, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFString(opt.stream_name, &ostream);
        WriteAMFNumber(opt.start, &ostream);
        WriteAMFNumber(opt.duration, &ostream);
        WriteAMFBool(opt.reset, &ostream);
        CHECK(ostream.good());
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg1(new policy::RtmpUnsentMessage);
    msg1->header.message_length = req_buf.size();
    msg1->header.message_type = policy::RTMP_MESSAGE_COMMAND_AMF0;
    msg1->header.stream_id = _message_stream_id;
    msg1->chunk_stream_id = _chunk_stream_id;
    msg1->body = req_buf;

    if (_client_impl->options().buffer_length_ms > 0) {
        char data[10];
        char* p = data;
        policy::WriteBigEndian2Bytes(
            &p, policy::RTMP_USER_CONTROL_EVENT_SET_BUFFER_LENGTH);
        policy::WriteBigEndian4Bytes(&p, stream_id());
        policy::WriteBigEndian4Bytes(&p, _client_impl->options().buffer_length_ms);
        policy::RtmpUnsentMessage* msg2 = policy::MakeUnsentControlMessage(
            policy::RTMP_MESSAGE_USER_CONTROL, data, sizeof(data));
        msg1->next.reset(msg2);
    }
    // FIXME(gejun): Do we need to SetChunkSize for play?
    // if (_client_impl->options().chunk_size > policy::RTMP_INITIAL_CHUNK_SIZE) {
    //     if (SetChunkSize(_client_impl->options().chunk_size) != 0) {
    //         return -1;
    //     }
    // }
    return _rtmpsock->Write(msg1);
}

int RtmpClientStream::Play2(const RtmpPlay2Options& opt) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_PLAY2, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFObject(opt, &ostream);
        if (!ostream.good()) {
            LOG(ERROR) << "Fail to serialize play2 request";
            errno = EINVAL;
            return -1;
        }
    }
    return SendMessage(0, policy::RTMP_MESSAGE_COMMAND_AMF0, req_buf);
}

const char* RtmpPublishType2Str(RtmpPublishType type) {
    switch (type) {
    case RTMP_PUBLISH_RECORD: return "record";
    case RTMP_PUBLISH_APPEND: return "append";
    case RTMP_PUBLISH_LIVE:   return "live";
    }
    return "Unknown RtmpPublishType";
}

bool Str2RtmpPublishType(const butil::StringPiece& str, RtmpPublishType* type) {
    if (str == "record") {
        *type = RTMP_PUBLISH_RECORD;
        return true;
    } else if (str == "append") {
        *type = RTMP_PUBLISH_APPEND;
        return true;
    } else if (str == "live") {
        *type = RTMP_PUBLISH_LIVE;
        return true;
    }
    return false;
}

int RtmpClientStream::Publish(const butil::StringPiece& name,
                              RtmpPublishType type) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_PUBLISH, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFString(name, &ostream);
        WriteAMFString(RtmpPublishType2Str(type), &ostream);
        CHECK(ostream.good());
    }
    return SendMessage(0, policy::RTMP_MESSAGE_COMMAND_AMF0, req_buf);
}

int RtmpClientStream::Seek(double offset_ms) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_SEEK, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFNumber(offset_ms, &ostream);
        CHECK(ostream.good());
    }
    return SendMessage(0, policy::RTMP_MESSAGE_COMMAND_AMF0, req_buf);    
}

int RtmpClientStream::Pause(bool pause_or_unpause, double offset_ms) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_PAUSE, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFBool(pause_or_unpause, &ostream);
        WriteAMFNumber(offset_ms, &ostream);
        CHECK(ostream.good());
    }
    return SendMessage(0, policy::RTMP_MESSAGE_COMMAND_AMF0, req_buf);
}

void RtmpClientStream::OnStatus(const RtmpInfo& info) {
    if (info.level() == RTMP_INFO_LEVEL_ERROR) {
        LOG(WARNING) << remote_side() << '[' << stream_id()
                     << "] " << info.code() << ": " << info.description();
        return SignalError();
    } else if (info.level() == RTMP_INFO_LEVEL_STATUS) {
        if ((!_options.play_name.empty() &&
             info.code() == RTMP_STATUS_CODE_PLAY_START) ||
            (!_options.publish_name.empty() &&
             info.code() == RTMP_STATUS_CODE_PUBLISH_START)) {
            // the memory fence makes sure that if _is_server_accepted is true, 
            // publish request must be sent (so that SendXXX functions can
            // be enabled)
            _is_server_accepted.store(true, butil::memory_order_release);
        }
    }
}

RtmpClientStreamOptions::RtmpClientStreamOptions()
    : share_connection(true)
    , wait_until_play_or_publish_is_sent(false)
    , create_stream_max_retry(3)
    , publish_type(RTMP_PUBLISH_LIVE) {
}

class OnClientStreamCreated : public google::protobuf::Closure {
public:
    void Run();  // @Closure
    void CancelBeforeCallMethod() { delete this; }
    
public:
    Controller cntl;
    // Hold a reference of stream to prevent it from destructing during an
    // async Create().
    butil::intrusive_ptr<RtmpClientStream> stream;
};

void OnClientStreamCreated::Run() {
    std::unique_ptr<OnClientStreamCreated> delete_self(this);
    if (cntl.Failed()) {
        LOG(WARNING) << "Fail to create stream=" << stream->rtmp_url()
                     << ": " << cntl.ErrorText();
        return;
    }
    if (stream->_created_stream_with_play_or_publish) {
        // the server accepted the play/publish command packed in createStream
        return;
    }
    const RtmpClientStreamOptions& options = stream->options();
    bool do_nothing = true;
    if (!options.play_name.empty()) {
        do_nothing = false;
        RtmpPlayOptions play_opt;
        play_opt.stream_name = options.play_name;
        if (stream->Play(play_opt) != 0) {
            LOG(WARNING) << "Fail to play " << options.play_name;
            return stream->SignalError();
        }
    }
    if (!options.publish_name.empty()) {
        do_nothing = false;
        if (stream->Publish(options.publish_name, options.publish_type) != 0) {
            LOG(WARNING) << "Fail to publish " << stream->rtmp_url();
            return stream->SignalError();
        }
    }
    if (do_nothing) {
        LOG(ERROR) << "play_name and publish_name are both empty";
        return stream->SignalError();
    }
}

void RtmpClientStream::Init(const RtmpClient* client,
                            const RtmpClientStreamOptions& options) {
    if (client->_impl == NULL) {
        LOG(FATAL) << "RtmpClient is not initialized";
        return OnStopInternal();
    }
    {
        std::unique_lock<butil::Mutex> mu(_state_mutex);
        if (_state == STATE_DESTROYING || _state == STATE_ERROR) {
            // already Destroy()-ed or SignalError()-ed
            LOG(WARNING) << "RtmpClientStream=" << this << " was already "
                "Destroy()-ed, stop Init()";
            return;
        }
    }
    _client_impl = client->_impl;
    _options = options;
    OnClientStreamCreated* done = new OnClientStreamCreated;
    done->stream.reset(this);
    done->cntl.set_stream_creator(this);
    done->cntl.set_connection_type(_options.share_connection ?
                                   CONNECTION_TYPE_SINGLE :
                                   CONNECTION_TYPE_SHORT);
    _from_socketmap = (done->cntl.connection_type() == CONNECTION_TYPE_SINGLE);
    done->cntl.set_max_retry(_options.create_stream_max_retry);
    if (_options.hash_code.has_been_set()) {
        done->cntl.set_request_code(_options.hash_code);
    }

    // Hack: we pass stream as response so that PackRtmpRequest can get
    // the stream from controller.
    google::protobuf::Message* res = (google::protobuf::Message*)this;
    const CallId call_id = done->cntl.call_id();
    {
        std::unique_lock<butil::Mutex> mu(_state_mutex);
        switch (_state) {
        case STATE_UNINITIALIZED:
            _state = STATE_CREATING;
            _create_stream_rpc_id = call_id;
            break;
        case STATE_CREATING:
        case STATE_CREATED:
            mu.unlock();
            LOG(ERROR) << "RtmpClientStream::Init() is called by multiple "
                "threads simultaneously";
            return done->CancelBeforeCallMethod();
        case STATE_ERROR:
        case STATE_DESTROYING:
            mu.unlock();
            return done->CancelBeforeCallMethod();
        }
    }
    _client_impl->_chan.CallMethod(NULL, &done->cntl, NULL, res, done);
    if (options.wait_until_play_or_publish_is_sent) {
        Join(call_id);
    }
}

std::string RtmpClientStream::rtmp_url() const {
    if (_client_impl == NULL) {
        return std::string();
    }
    butil::StringPiece tcurl = _client_impl->options().tcUrl;
    butil::StringPiece stream_name = _options.stream_name();
    std::string result;
    result.reserve(tcurl.size() + 1 + stream_name.size());
    result.append(tcurl.data(), tcurl.size());
    result.push_back('/');
    result.append(stream_name.data(), stream_name.size());
    return result;
}

// ========= RtmpRetryingClientStream ============

RtmpRetryingClientStreamOptions::RtmpRetryingClientStreamOptions()
    : retry_interval_ms(1000)
    , max_retry_duration_ms(-1)
    , fast_retry_count(2)
    , quit_when_no_data_ever(true) {
}

RtmpRetryingClientStream::RtmpRetryingClientStream()
    : RtmpStreamBase(true)
    , _destroying(false)
    , _called_on_stop(false)
    , _changed_stream(false)
    , _has_timer_ever(false)
    , _is_server_accepted_ever(false)
    , _num_fast_retries(0)
    , _last_creation_time_us(0)
    , _last_retry_start_time_us(0)
    , _create_timer_id(0)
    , _sub_stream_creator(NULL) {
    get_rtmp_bvars()->retrying_client_stream_count << 1;
    _self_ref.reset(this);
}

RtmpRetryingClientStream::~RtmpRetryingClientStream() {
    delete _sub_stream_creator;
    _sub_stream_creator = NULL;
    get_rtmp_bvars()->retrying_client_stream_count << -1;
}

void RtmpRetryingClientStream::CallOnStopIfNeeded() {
    // CallOnStop uses locks, we don't need memory fence on _called_on_stop,
    // atomic ops is enough.
    if (!_called_on_stop.load(butil::memory_order_relaxed) &&
        !_called_on_stop.exchange(true, butil::memory_order_relaxed)) {
        CallOnStop();
    }
}        

void RtmpRetryingClientStream::Destroy() {
    if (_destroying.exchange(true, butil::memory_order_relaxed)) {
        // Destroy() was already called.
        return;
    }

    // Make sure _self_ref is released before quiting this function.
    // Notice that _self_ref.reset(NULL) is wrong because it may destructs
    // this object immediately.
    butil::intrusive_ptr<RtmpRetryingClientStream> self_ref;
    _self_ref.swap(self_ref);

    butil::intrusive_ptr<RtmpStreamBase> old_sub_stream;
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        // swap instead of reset(NULL) to make the stream destructed
        // outside _stream_mutex.
        _using_sub_stream.swap(old_sub_stream);
    }
    if (old_sub_stream) {
        old_sub_stream->Destroy();
    }
    
    if (_has_timer_ever) {
        if (bthread_timer_del(_create_timer_id) == 0) {
            // The callback is not run yet. Remove the additional ref added
            // before creating the timer.
            butil::intrusive_ptr<RtmpRetryingClientStream> deref(this, false);
        }
    }
    return CallOnStopIfNeeded();
}

void RtmpRetryingClientStream::Init(
    SubStreamCreator* sub_stream_creator,
    const RtmpRetryingClientStreamOptions& options) {
    if (sub_stream_creator == NULL) {
        LOG(ERROR) << "sub_stream_creator is NULL";
        return CallOnStopIfNeeded();
    }
    _sub_stream_creator = sub_stream_creator;
    if (_destroying.load(butil::memory_order_relaxed)) {
        LOG(WARNING) << "RtmpRetryingClientStream=" << this << " was already "
            "Destroy()-ed, stop Init()";
        return;
    }
    _options = options;
    // retrying stream does not support this option.
    _options.wait_until_play_or_publish_is_sent = false;
    _last_retry_start_time_us = butil::gettimeofday_us();
    Recreate();
}

void RetryingClientMessageHandler::OnPlayable() {
    _parent->OnPlayable();
}

void RetryingClientMessageHandler::OnUserData(void* msg) {
    _parent->CallOnUserData(msg);
}

void RetryingClientMessageHandler::OnCuePoint(brpc::RtmpCuePoint* cuepoint) {
    _parent->CallOnCuePoint(cuepoint);
}

void RetryingClientMessageHandler::OnMetaData(brpc::RtmpMetaData* metadata, const butil::StringPiece& name) {
    _parent->CallOnMetaData(metadata, name);
}

void RetryingClientMessageHandler::OnAudioMessage(brpc::RtmpAudioMessage* msg) {
    _parent->CallOnAudioMessage(msg);
}

void RetryingClientMessageHandler::OnVideoMessage(brpc::RtmpVideoMessage* msg) {
    _parent->CallOnVideoMessage(msg);
}

void RetryingClientMessageHandler::OnSharedObjectMessage(RtmpSharedObjectMessage* msg) {
    _parent->CallOnSharedObjectMessage(msg);
}

void RetryingClientMessageHandler::OnSubStreamStop(RtmpStreamBase* sub_stream) {
    _parent->OnSubStreamStop(sub_stream);
}

RetryingClientMessageHandler::RetryingClientMessageHandler(RtmpRetryingClientStream* parent)
    : _parent(parent) {}

void RtmpRetryingClientStream::Recreate() {
    butil::intrusive_ptr<RtmpStreamBase> sub_stream;
    _sub_stream_creator->NewSubStream(new RetryingClientMessageHandler(this), &sub_stream);
    butil::intrusive_ptr<RtmpStreamBase> old_sub_stream;
    bool destroying = false;
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        // Need to check _destroying to avoid setting the new sub_stream to a 
        // destroying retrying stream. 
        // Note: the load of _destroying and the setting of _using_sub_stream 
        // must be in the same lock, otherwise current bthread may be scheduled
        // and Destroy() may be called, making new sub_stream leaked.
        destroying = _destroying.load(butil::memory_order_relaxed);
        if (!destroying) {
            _using_sub_stream.swap(old_sub_stream);
            _using_sub_stream = sub_stream;
            _changed_stream = true;
        }
    }
    if (old_sub_stream) {
        old_sub_stream->Destroy();
    }
    if (destroying) {
        sub_stream->Destroy();
        return;
    }
    _last_creation_time_us = butil::gettimeofday_us();
    // If Init() of sub_stream is called before setting _using_sub_stream,
    // OnStop() may happen before _using_sub_stream is set and the stopped
    // stream is wrongly left in the variable.
     
    _sub_stream_creator->LaunchSubStream(sub_stream.get(), &_options);
}

void RtmpRetryingClientStream::OnRecreateTimer(void* arg) {
    // Hold the referenced stream.
    butil::intrusive_ptr<RtmpRetryingClientStream> ptr(
        static_cast<RtmpRetryingClientStream*>(arg), false/*not add ref*/);
    ptr->Recreate();
}

void RtmpRetryingClientStream::OnSubStreamStop(RtmpStreamBase* sub_stream) {
    // Make sure the sub_stream is destroyed after this function.
    DestroyingPtr<RtmpStreamBase> sub_stream_guard(sub_stream);
    
    butil::intrusive_ptr<RtmpStreamBase> removed_sub_stream;
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        if (sub_stream == _using_sub_stream) {
            _using_sub_stream.swap(removed_sub_stream);
        }
    }
    if (removed_sub_stream == NULL ||
        _destroying.load(butil::memory_order_relaxed) ||
        _called_on_stop.load(butil::memory_order_relaxed)) {
        return;
    }
    // Update _is_server_accepted_ever
    if (sub_stream->is_server_accepted()) {
        _is_server_accepted_ever = true;
    }
    
    if (_options.max_retry_duration_ms == 0) {
        return CallOnStopIfNeeded();
    }
    // If the sub_stream has data ever, count this retry as the beginning
    // of RtmpRetryingClientStreamOptions.max_retry_duration_ms.
    if ((!_options.play_name.empty() && sub_stream->has_data_ever()) ||
        (!_options.publish_name.empty() && sub_stream->is_server_accepted())) {
        const int64_t now = butil::gettimeofday_us();
        if (now >= _last_retry_start_time_us +
            3 * _options.retry_interval_ms * 1000L) {
            // re-enable fast retries when the interval is long enough.
            // `3' is just a randomly-chosen (small) number.
            _num_fast_retries = 0;
        }
        _last_retry_start_time_us = now;
    }
    // Check max duration. Notice that this branch cannot be moved forward
    // above branch which may update _last_retry_start_time_us
    if (_options.max_retry_duration_ms > 0 &&
        butil::gettimeofday_us() >
        (_last_retry_start_time_us + _options.max_retry_duration_ms * 1000L)) {
        // exceed the duration, stop retrying.
        return CallOnStopIfNeeded();
    }
    if (_num_fast_retries < _options.fast_retry_count) {
        ++_num_fast_retries;
        // Retry immediately for several times. Works for scenarios like:
        // restarting servers, occasional connection lost etc...
        return Recreate();
    }
    if (_options.quit_when_no_data_ever &&
        ((!_options.play_name.empty() && !has_data_ever()) ||
         (!_options.publish_name.empty() && !_is_server_accepted_ever))) {
        // Stop retrying when created playing streams never have data or
        // publishing streams were never accepted. It's very likely that
        // continuing retrying does not make sense.
        return CallOnStopIfNeeded();
    }
    const int64_t wait_us = _last_creation_time_us +
        _options.retry_interval_ms * 1000L - butil::gettimeofday_us();
    if (wait_us > 0) {
        // retry is too frequent, schedule the retry.
        // Add a ref for OnRecreateTimer which does deref.
        butil::intrusive_ptr<RtmpRetryingClientStream>(this).detach();
        if (bthread_timer_add(&_create_timer_id,
                              butil::microseconds_from_now(wait_us),
                              OnRecreateTimer, this) != 0) {
            LOG(ERROR) << "Fail to create timer";
            return CallOnStopIfNeeded();
        }
        _has_timer_ever = true;
    } else {
        Recreate();
    }
}

int RtmpRetryingClientStream::AcquireStreamToSend(
    butil::intrusive_ptr<RtmpStreamBase>* ptr) {
    BAIDU_SCOPED_LOCK(_stream_mutex);
    if (!_using_sub_stream) {
        errno = EPERM;
        return -1;
    }
    if (!_using_sub_stream->is_server_accepted()) {
        // not published yet.
        errno = EPERM;
        return -1;
    }
    if (_changed_stream) {
        _changed_stream = false;
        errno = ERTMPPUBLISHABLE;
        return -1;
    }
    *ptr = _using_sub_stream;
    return 0;
}

int RtmpRetryingClientStream::SendCuePoint(const RtmpCuePoint& obj) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendCuePoint(obj);
}

int RtmpRetryingClientStream::SendMetaData(const RtmpMetaData& obj, const butil::StringPiece& name) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendMetaData(obj, name);
}

int RtmpRetryingClientStream::SendSharedObjectMessage(
    const RtmpSharedObjectMessage& msg) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendSharedObjectMessage(msg);
}

int RtmpRetryingClientStream::SendAudioMessage(const RtmpAudioMessage& msg) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendAudioMessage(msg);
}

int RtmpRetryingClientStream::SendAACMessage(const RtmpAACMessage& msg) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendAACMessage(msg);
}

int RtmpRetryingClientStream::SendVideoMessage(const RtmpVideoMessage& msg) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendVideoMessage(msg);
}

int RtmpRetryingClientStream::SendAVCMessage(const RtmpAVCMessage& msg) {
    butil::intrusive_ptr<RtmpStreamBase> ptr;
    if (AcquireStreamToSend(&ptr) != 0) {
        return -1;
    }
    return ptr->SendAVCMessage(msg);
}

void RtmpRetryingClientStream::StopCurrentStream() {
    butil::intrusive_ptr<RtmpStreamBase> sub_stream;
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        sub_stream = _using_sub_stream;
    }
    if (sub_stream) {
        sub_stream->SignalError();
    }
}

void RtmpRetryingClientStream::OnPlayable() {}

butil::EndPoint RtmpRetryingClientStream::remote_side() const {
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        if (_using_sub_stream) {
            return _using_sub_stream->remote_side();
        }
    }
    return butil::EndPoint();
}

butil::EndPoint RtmpRetryingClientStream::local_side() const {
    {
        BAIDU_SCOPED_LOCK(_stream_mutex);
        if (_using_sub_stream) {
            return _using_sub_stream->local_side();
        }
    }
    return butil::EndPoint();
}

// =========== RtmpService ===============
void RtmpService::OnPingResponse(const butil::EndPoint&, uint32_t) {
    // TODO: put into some bvars?
}

RtmpServerStream::RtmpServerStream()
    : RtmpStreamBase(false)
    , _client_supports_stream_multiplexing(false)
    , _is_publish(false)
    , _onfail_id(INVALID_BTHREAD_ID) {
    get_rtmp_bvars()->server_stream_count << 1;
}

RtmpServerStream::~RtmpServerStream() {
    get_rtmp_bvars()->server_stream_count << -1;
}

void RtmpServerStream::Destroy() {
    CHECK(false) << "You're not supposed to call Destroy() for server-side streams";
}

void RtmpServerStream::OnPlay(const RtmpPlayOptions& opt,
                              butil::Status* status,
                              google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    status->set_error(EPERM, "%s[%u] ignored play{stream_name=%s start=%f"
                      " duration=%f reset=%d}",
                      butil::endpoint2str(remote_side()).c_str(), stream_id(),
                      opt.stream_name.c_str(), opt.start, opt.duration,
                      (int)opt.reset);
}

void RtmpServerStream::OnPlay2(const RtmpPlay2Options& opt) {
    LOG(ERROR) << remote_side() << '[' << stream_id()
               << "] ignored play2{" << opt.ShortDebugString() << '}';
}

void RtmpServerStream::OnPublish(const std::string& name,
                                 RtmpPublishType type,
                                 butil::Status* status,
                                 google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    status->set_error(EPERM, "%s[%u] ignored publish{stream_name=%s type=%s}",
                      butil::endpoint2str(remote_side()).c_str(), stream_id(),
                      name.c_str(), RtmpPublishType2Str(type));
}

int RtmpServerStream::OnSeek(double offset_ms) {
    LOG(ERROR) << remote_side() << '[' << stream_id() << "] ignored seek("
               << offset_ms << ")";
    return -1;
}

int RtmpServerStream::OnPause(bool pause, double offset_ms) {
    LOG(ERROR) << remote_side() << '[' << stream_id() << "] ignored "
               << (pause ? "pause" : "unpause")
               << "(offset_ms=" << offset_ms << ")";
    return -1;
}

void RtmpServerStream::OnSetBufferLength(uint32_t /*buffer_length_ms*/) {}

int RtmpServerStream::SendStopMessage(const butil::StringPiece& error_desc) {
    if (_rtmpsock == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (FLAGS_rtmp_server_close_connection_on_error &&
        !_client_supports_stream_multiplexing) {
        _rtmpsock->SetFailed(EFAILEDSOCKET, "Close connection because %.*s",
                             (int)error_desc.size(), error_desc.data());
        // The purpose is to close the connection, no matter what SetFailed()
        // returns, the operation should be done.
        LOG_IF(WARNING, FLAGS_log_error_text)
            << "Close connection because " << error_desc;
        return 0;
    }

    // Send StreamNotFound error to make the client close connections.
    // Works for flashplayer and ffplay(not started playing), not work for SRS
    // and ffplay(started playing)
    butil::IOBuf req_buf;
    RtmpInfo info;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        if (_is_publish) {
            // NetStream.Publish.Rejected does not work for ffmpeg, works for OBS.
            // NetStream.Publish.BadName does not work for OBS.
            // NetStream.Play.StreamNotFound is not accurate but works for both
            // ffmpeg and OBS.
            info.set_code(RTMP_STATUS_CODE_STREAM_NOT_FOUND);
        } else {
            info.set_code(RTMP_STATUS_CODE_STREAM_NOT_FOUND);
        }
        info.set_level(RTMP_INFO_LEVEL_ERROR);
        if (!error_desc.empty()) {
            info.set_description(error_desc.as_string());
        }
        WriteAMFObject(info, &ostream);
    }
    SocketMessagePtr<policy::RtmpUnsentMessage> msg(new policy::RtmpUnsentMessage);
    msg->header.message_length = req_buf.size();
    msg->header.message_type = policy::RTMP_MESSAGE_COMMAND_AMF0;
    msg->header.stream_id = _message_stream_id;
    msg->chunk_stream_id = _chunk_stream_id;
    msg->body = req_buf;
    
    if (policy::WriteWithoutOvercrowded(_rtmpsock.get(), msg) != 0) {
        PLOG_IF(WARNING, errno != EFAILEDSOCKET)
            << _rtmpsock->remote_side() << '[' << _message_stream_id
            << "]: Fail to send " << info.code() << ": " << error_desc;
        return -1;
    }
    LOG_IF(WARNING, FLAGS_log_error_text)
        << _rtmpsock->remote_side() << '[' << _message_stream_id << "]: Sent "
        << info.code() << ' ' << error_desc;
    return 0;
}

// Call this method to send StreamDry to the client.
// Returns 0 on success, -1 otherwise.
int RtmpServerStream::SendStreamDry() {
    char data[6];
    char* p = data;
    policy::WriteBigEndian2Bytes(&p, policy::RTMP_USER_CONTROL_EVENT_STREAM_DRY);
    policy::WriteBigEndian4Bytes(&p, stream_id());
    return SendControlMessage(policy::RTMP_MESSAGE_USER_CONTROL, data, sizeof(data));
}

int RtmpServerStream::RunOnFailed(bthread_id_t id, void* data, int) {
    butil::intrusive_ptr<RtmpServerStream> stream(
        static_cast<RtmpServerStream*>(data), false);
    CHECK(stream->_rtmpsock);
    stream->OnStopInternal();
    bthread_id_unlock_and_destroy(id);
    return 0;
}

void RtmpServerStream::OnStopInternal() {
    if (_rtmpsock == NULL) {
        return CallOnStop();
    }
    policy::RtmpContext* ctx =
        static_cast<policy::RtmpContext*>(_rtmpsock->parsing_context());
    if (ctx == NULL) {
        LOG(FATAL) << _rtmpsock->remote_side() << ": RtmpContext of "
                   << *_rtmpsock << " is NULL";
        return CallOnStop();
    }
    if (ctx->RemoveMessageStream(this)) {
        return CallOnStop();
    }
}

butil::StringPiece RemoveRtmpPrefix(const butil::StringPiece& url_in) {
    if (!url_in.starts_with("rtmp://")) {
        return url_in;
    }
    butil::StringPiece url = url_in;
    size_t i = 7;
    for (; i < url.size() && url[i] == '/'; ++i);
    url.remove_prefix(i);
    return url;
}

butil::StringPiece RemoveProtocolPrefix(const butil::StringPiece& url_in) {
    size_t proto_pos = url_in.find("://");
    if (proto_pos == butil::StringPiece::npos) {
        return url_in;
    }
    butil::StringPiece url = url_in;
    size_t i = proto_pos + 3;
    for (; i < url.size() && url[i] == '/'; ++i);
    url.remove_prefix(i);
    return url;
}

void ParseRtmpHostAndPort(const butil::StringPiece& host_and_port,
                          butil::StringPiece* host,
                          butil::StringPiece* port) {
    size_t colon_pos = host_and_port.find(':');
    if (colon_pos == butil::StringPiece::npos) {
        if (host) {
            *host = host_and_port;
        }
        if (port) {
            *port = "1935";
        }
    } else {
        if (host) {
            *host = host_and_port.substr(0, colon_pos);
        }
        if (port) {
            *port = host_and_port.substr(colon_pos + 1);
        }
    }
}

butil::StringPiece RemoveQueryStrings(const butil::StringPiece& stream_name_in,
                                     butil::StringPiece* query_strings) {
    const size_t qm_pos = stream_name_in.find('?');
    if (qm_pos == butil::StringPiece::npos) {
        if (query_strings) {
            query_strings->clear();
        }
        return stream_name_in;
    } else {
        if (query_strings) {
            *query_strings = stream_name_in.substr(qm_pos + 1);
        }
        return stream_name_in.substr(0, qm_pos);
    }
}

// Split vhost from *app in forms of "APP?vhost=..." and overwrite *host.
static void SplitVHostFromApp(const butil::StringPiece& app_and_vhost,
                              butil::StringPiece* app,
                              butil::StringPiece* vhost) {
    const size_t q_pos = app_and_vhost.find('?');
    if (q_pos == butil::StringPiece::npos) {
        if (app) {
            *app = app_and_vhost;
        }
        if (vhost) {
            vhost->clear();
        }
        return;
    }
    
    if (app) {
        *app = app_and_vhost.substr(0, q_pos);
    }
    if (vhost) {
        butil::StringPiece qstr = app_and_vhost.substr(q_pos + 1);
        butil::StringSplitter sp(qstr.data(), qstr.data() + qstr.size(), '&');
        for (; sp; ++sp) {
            butil::StringPiece field(sp.field(), sp.length());
            if (field.starts_with("vhost=")) {
                *vhost = field.substr(6);
                // vhost cannot have port.
                const size_t colon_pos = vhost->find_last_of(':');
                if (colon_pos != butil::StringPiece::npos) {
                    vhost->remove_suffix(vhost->size() - colon_pos);
                }
                return;
            }
        }
        vhost->clear();
    }
}

void ParseRtmpURL(const butil::StringPiece& rtmp_url_in,
                  butil::StringPiece* host,
                  butil::StringPiece* vhost,
                  butil::StringPiece* port,
                  butil::StringPiece* app,
                  butil::StringPiece* stream_name) {
    if (stream_name) {
        stream_name->clear();
    }
    butil::StringPiece rtmp_url = RemoveRtmpPrefix(rtmp_url_in);
    size_t slash1_pos = rtmp_url.find_first_of('/');
    if (slash1_pos == butil::StringPiece::npos) {
        if (host || port) {
            ParseRtmpHostAndPort(rtmp_url, host, port);
        }
        if (app) {
            app->clear();
        }
        return;
    }
    if (host || port) {
        ParseRtmpHostAndPort(rtmp_url.substr(0, slash1_pos), host, port);
    }
    // Remove duplicated slashes.
    for (++slash1_pos; slash1_pos < rtmp_url.size() &&
             rtmp_url[slash1_pos] == '/'; ++slash1_pos);
    rtmp_url.remove_prefix(slash1_pos);
    size_t slash2_pos = rtmp_url.find_first_of('/');
    if (slash2_pos == butil::StringPiece::npos) {
        return SplitVHostFromApp(rtmp_url, app, vhost);
    }
    SplitVHostFromApp(rtmp_url.substr(0, slash2_pos), app, vhost);
    if (stream_name != NULL) {
        // Remove duplicated slashes.
        for (++slash2_pos; slash2_pos < rtmp_url.size() &&
                 rtmp_url[slash2_pos] == '/'; ++slash2_pos);
        rtmp_url.remove_prefix(slash2_pos);
        *stream_name = rtmp_url;
    }
}

std::string MakeRtmpURL(const butil::StringPiece& host,
                        const butil::StringPiece& port,
                        const butil::StringPiece& app,
                        const butil::StringPiece& stream_name) {
    std::string result;
    result.reserve(15 + host.size() + app.size() + stream_name.size());
    result.append("rtmp://");
    result.append(host.data(), host.size());
    if (!port.empty()) {
        result.push_back(':');
        result.append(port.data(), port.size());
    }
    if (!app.empty()) {
        result.push_back('/');
        result.append(app.data(), app.size());
    }
    if (!stream_name.empty()) {
        if (app.empty()) {  // extra / to notify user that app is empty.
            result.push_back('/');
        }
        result.push_back('/');
        result.append(stream_name.data(), stream_name.size());
    }
    return result;
}

} // namespace brpc
