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

// [Modified from the code in SRS2 (src/kernel/srs_kernel_ts.cpp)]
// The MIT License (MIT)
// Copyright (c) 2013-2015 SRS(ossrs)
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "brpc/log.h"
#include "brpc/policy/rtmp_protocol.h"
#include "brpc/ts.h"


namespace brpc {

// MPEG2 transport stream (aka DVB) mux
// Copyright (c) 2003 Fabrice Bellard.
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
static const u_int32_t crc_table[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
    0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
    0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
    0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
    0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
    0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
    0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
    0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
    0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
    0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
    0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
    0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
    0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
    0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
    0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
    0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
    0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
    0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
    0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
    0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
    0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
    0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
    0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
    0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
    0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
    0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
    0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
    0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

// http://www.stmc.edu.hk/~vincent/ffmpeg_0.4.9-pre1/libavformat/mpegtsenc.c
uint32_t mpegts_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < len; ++i) {
        crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *p++) & 0xff];
    }
    return crc;
}

// Transport Stream packets are 188 bytes in length.
static const size_t TS_PACKET_SIZE = 188;

static const int TS_PMT_NUMBER = 1;

// Table 2-3 - PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
// NOTE - The transport packets with PID values 0x0000, 0x0001, and
// 0x0010-0x1FFE are allowed to carry a PCR.
// Program Association Table(see Table 2-25)
static const TsPid TS_PID_PAT = (TsPid)0x00;
// Conditional Access Table (see Table 2-27)
//static const TsPid TS_PID_CAT = (TsPid)0x01;
// Transport Stream Description Table
//static const TsPid TS_PID_TSDT = (TsPid)0x02;
//static const TsPid TS_PID_RESERVED_START = (TsPid)0x03;
// static const TsPid TS_PID_RESERVED_END = (TsPid)0x0f;
// May be assigned as network_PID; Program_map_PID; elementary_PID; or for
// other purposes
//static const TsPid TS_PID_APP_START = (TsPid)0x10;
static const TsPid TS_PID_VIDEO_AVC = (TsPid)0x100;
static const TsPid TS_PID_AUDIO_AAC = (TsPid)0x101;
static const TsPid TS_PID_AUDIO_MP3 = (TsPid)0x102;
static const TsPid TS_PID_PMT = (TsPid)0x1001;
//static const TsPid TS_PID_APP_END = (TsPid)0x1ffe;
static const TsPid TS_PID_NULL = (TsPid)0x01FFF;// null packets (see Table 2-3)

// The sync_byte is a fixed 8-bit field whose value is '0100 0111' (0x47).
// Sync_byte emulation in the choice of values for other regularly
// occurring fields, such as PID, should be avoided.
static const int8_t TS_SYNC_BYTE = 0x47; // 8 bits

const char* TsStream2Str(TsStream stream) {
    switch (stream) {
    case TS_STREAM_RESERVED: return "Reserved";
    case TS_STREAM_AUDIO_MP3: return "MP3";
    case TS_STREAM_AUDIO_AAC: return "AAC";
    case TS_STREAM_AUDIO_AC3: return "AC3";
    case TS_STREAM_AUDIO_DTS: return "AudioDTS";
    case TS_STREAM_VIDEO_H264: return "H.264";
    case TS_STREAM_VIDEO_MPEG4: return "MP4";
    case TS_STREAM_AUDIO_MPEG4: return "MP4A";
    }
    return "Other";
}

// the media audio/video message parsed from PES packet.
struct TsWriter::TsMessage {
public:
    TsMessage()
        : write_pcr(false)
        , is_discontinuity(false)
        , dts(0)
        , pts(0)
        , sid(TS_PES_STREAM_ID_UNKNOWN)
        , PES_packet_length(0)
        , continuity_counter(0) {
    }

    // whether this message with pcr(program_clock_reference)
    // generally, the video IDR(keyframe) carries the pcr info.
    bool write_pcr;
    
    // whether got discontinuity ts, for example, sequence header changed.
    // TODO: always false right now.
    bool is_discontinuity;
    
    // the timestamp in 90khz
    int64_t dts;
    int64_t pts;
    
    // the id of pes stream to indicate the payload codec.
    TsPESStreamId sid;
    
    // size of payload, 0 indicates the length() of payload.
    uint16_t PES_packet_length;
    
    uint8_t continuity_counter;
    
    butil::IOBuf payload;
};

// whether the sid indicates the elementary stream audio.
inline bool is_audio(TsPESStreamId sid) {
    return ((sid >> 5) & 0x07) == 0x06/*0b110*/;
}

// whether the sid indicates the elementary stream video.
inline bool is_video(TsPESStreamId sid) {
    return ((sid >> 4) & 0x0f) == 0x0e/*0b1110*/;
}

TsChannelGroup::TsChannelGroup() {}
TsChannelGroup::~TsChannelGroup() {}

TsChannel* TsChannelGroup::get(TsPid pid) {
    return &_pids[pid];
    // std::map<TsPid, TsChannel>::iterator it = _pids.find(pid);
    // if (it != _pids.end()) {
    //     return &it->second;
    // }
    // return NULL;
}

TsChannel* TsChannelGroup::set(TsPid pid) {
    std::map<TsPid, TsChannel>::iterator it = _pids.find(pid);
    if (it == _pids.end()) {
        return &_pids[pid];
    } else {
        return &it->second;
    }
}

TsPacket::TsPacket(TsChannelGroup* g)
    : _modified(false)
    , _transport_error_indicator(0)
    , _payload_unit_start_indicator(0)
    , _transport_priority(0)
    , _pid(TS_PID_PAT)
    , _transport_scrambling_control(TS_SCRAMBLED_DISABLED)
    , _adaptation_field_control(TS_AF_RESERVED)
    , _continuity_counter(0)
    , _adaptation_field(NULL)
    , _payload(NULL)
    , _tschan_group(g) {
}

TsPacket::~TsPacket() {
    delete _adaptation_field;
    delete _payload;
}

void TsPacket::Reset() {
    delete _payload;
    _payload = NULL;
    delete _adaptation_field;
    _adaptation_field = NULL;
    _transport_error_indicator = 0;
    _payload_unit_start_indicator = 0;
    _transport_priority = 0;
    _pid = TS_PID_PAT;
    _transport_scrambling_control = TS_SCRAMBLED_DISABLED;
    _adaptation_field_control = TS_AF_RESERVED;
    _continuity_counter = 0;
    _payload = NULL;
    _modified = false;
}

TsAdaptationField* TsPacket::CreateAdaptationField() {
    if (_adaptation_field != NULL) {
        LOG(ERROR) << "_adaptation_field is not NULL";
        return _adaptation_field;
    }
    _adaptation_field = new TsAdaptationField;
    if (_adaptation_field_control == TS_AF_RESERVED) {
        _adaptation_field_control = TS_AF_ADAPTATION_ONLY;
    } else if (_adaptation_field_control == TS_AF_PAYLOAD_ONLY) {
        _adaptation_field_control = TS_AF_BOTH;
    } else {
        LOG(ERROR) << "Invalid _adaptation_field_control="
                   << _adaptation_field_control;
    }
    return _adaptation_field;
}

size_t TsPacket::ByteSize() const {
    size_t sz = 4;
    if (_adaptation_field) {
        sz +=_adaptation_field->ByteSize();
    }
    if (_payload) {
        sz += _payload->ByteSize();
    }
    return sz;
}

int TsPacket::Encode(void* data) const {
    char* p = (char*)data;

    policy::Write1Byte(&p, TS_SYNC_BYTE);

    int16_t pidv = _pid & 0x1FFF;
    pidv |= (_transport_priority << 13) & 0x2000;
    pidv |= (_transport_error_indicator << 15) & 0x8000;
    pidv |= (_payload_unit_start_indicator << 14) & 0x4000;
    policy::WriteBigEndian2Bytes(&p, pidv);

    int8_t ccv = _continuity_counter & 0x0F;
    ccv |= (_transport_scrambling_control << 6) & 0xC0;
    TsAdaptationFieldType af_control = _adaptation_field_control;
    if (af_control == TS_AF_RESERVED) {
        // In the case of a null packet, af_control shall be set to '01'.
        af_control = TS_AF_PAYLOAD_ONLY;
    }
    ccv |= (af_control << 4) & 0x30;
    policy::Write1Byte(&p, ccv);
    
    if (_adaptation_field) {
        if (_adaptation_field->Encode(p, af_control) != 0) {
            LOG(ERROR) << "Fail to encode _adaptation_field";
            return -1;
        }
        p += _adaptation_field->ByteSize();
    }
    
    if (_payload) {
        if (_payload->Encode(p) != 0) {
            LOG(ERROR) << "Fail to encode _payload";
            return -1;
        }
        p += _payload->ByteSize();
    }
    return 0;
}

void TsPacket::AddPadding(size_t num_stuffings) {
    const bool no_af_before = (_adaptation_field == NULL);
    TsAdaptationField* af = mutable_adaptation_field();
    if (no_af_before) {
        const size_t sz = af->ByteSize();
        if (num_stuffings > sz) {
            af->nb_af_reserved = num_stuffings - sz;
        }
    } else {
        af->nb_af_reserved += num_stuffings;
    }
}

void TsPacket::CreateAsPAT(int16_t pmt_number, TsPid pmt_pid) {
    if (_modified) {
        Reset();
    }
    _pid = TS_PID_PAT;
    _payload_unit_start_indicator = 1;
    _adaptation_field_control = TS_AF_PAYLOAD_ONLY;
    
    TsPayloadPAT* pat = new TsPayloadPAT(this);
    pat->pointer_field = 0;
    pat->table_id = TS_PSI_ID_PAS;
    pat->section_syntax_indicator = 1;
    pat->transport_stream_id = 1;
    pat->version_number = 0;
    pat->current_next_indicator = 1;
    pat->section_number = 0;
    pat->last_section_number = 0;
    pat->programs.push_back(TsPayloadPATProgram(pmt_number, pmt_pid));
    _payload = pat;
}

int TsPacket::CreateAsPMT(int16_t pmt_number, TsPid pmt_pid,
                         TsPid vpid, TsStream vs,
                         TsPid apid, TsStream as) {
    if (vs != TS_STREAM_VIDEO_H264 &&
        as != TS_STREAM_AUDIO_AAC && as != TS_STREAM_AUDIO_MP3) {
        LOG(ERROR) << "Unsupported video_stream=" << vs << " audio_stream=" << as;
        return -1;
    }

    if (_modified) {
        Reset();
    }
    _pid = pmt_pid;
    _payload_unit_start_indicator = 1;
    _adaptation_field_control = TS_AF_PAYLOAD_ONLY;

    TsPayloadPMT* pmt = new TsPayloadPMT(this);
    pmt->pointer_field = 0;
    pmt->table_id = TS_PSI_ID_PMS;
    pmt->section_syntax_indicator = 1;
    pmt->program_number = pmt_number;
    pmt->version_number = 0;
    pmt->current_next_indicator = 1;
    pmt->section_number = 0;
    pmt->last_section_number = 0;
    pmt->program_info_length = 0;
    if (as == TS_STREAM_AUDIO_AAC || as == TS_STREAM_AUDIO_MP3) {
        // use audio to carry pcr by default.
        // for hls, there must be at least one audio channel.
        pmt->PCR_PID = apid;
        pmt->infos.push_back(new TsPayloadPMTESInfo(as, apid));
    }
    if (vs == TS_STREAM_VIDEO_H264) {
        // if h.264 specified, use video to carry pcr.
        pmt->PCR_PID = vpid;
        pmt->infos.push_back(new TsPayloadPMTESInfo(vs, vpid));
    }
    _payload = pmt;
    return 0;
}

void TsPacket::CreateAsPESFirst(TsPid pid, TsPESStreamId sid,
                                uint8_t continuity_counter, bool discontinuity, 
                                int64_t pcr, int64_t dts, int64_t pts, int size) {
    if (_modified) {
        Reset();
    }
    _pid = pid;
    _payload_unit_start_indicator = 1;
    _adaptation_field_control = TS_AF_PAYLOAD_ONLY;
    _continuity_counter = continuity_counter;
    TsPayloadPES* pes = new TsPayloadPES(this);
    pes->stream_id = sid;
    pes->PES_packet_length = (size > 0xFFFF ? 0 : size);
    pes->PES_scrambling_control = 0;
    pes->PES_priority = 0;
    pes->data_alignment_indicator = 0;
    pes->copyright = 0;
    pes->original_or_copy = 0;
    pes->PTS_DTS_flags = (dts == pts)? 0x02:0x03;
    pes->ESCR_flag = 0;
    pes->ES_rate_flag = 0;
    pes->DSM_trick_mode_flag = 0;
    pes->additional_copy_info_flag = 0;
    pes->PES_CRC_flag = 0;
    pes->PES_extension_flag = 0;
    pes->pts = pts;
    pes->dts = dts;
    _payload = pes;
    
    if (pcr >= 0) {
        TsAdaptationField* af = mutable_adaptation_field();
        af->discontinuity_indicator = discontinuity;
        af->random_access_indicator = 0;
        af->elementary_stream_priority_indicator = 0;
        af->PCR_flag = 1;
        af->OPCR_flag = 0;
        af->splicing_point_flag = 0;
        af->transport_private_data_flag = 0;
        af->adaptation_field_extension_flag = 0;
        af->program_clock_reference_base = pcr;
        af->program_clock_reference_extension = 0;
    }
}

void TsPacket::CreateAsPESContinue(TsPid pid, uint8_t continuity_counter) {
    if (_modified) {
        Reset();
    }
    _pid = pid;
    _adaptation_field_control = TS_AF_PAYLOAD_ONLY;
    _continuity_counter = continuity_counter;
}

TsAdaptationField::TsAdaptationField()
    : discontinuity_indicator(0)
    , random_access_indicator(0)
    , elementary_stream_priority_indicator(0)
    , PCR_flag(0)
    , OPCR_flag(0)
    , splicing_point_flag(0)
    , transport_private_data_flag(0)
    , adaptation_field_extension_flag(0)
    , program_clock_reference_base(0)
    , program_clock_reference_extension(0)
    , original_program_clock_reference_base(0)
    , original_program_clock_reference_extension(0)
    , splice_countdown(0)
    , transport_private_data_length(0)
    , transport_private_data(NULL)
    , adaptation_field_extension_length(0)
    , ltw_flag(0)
    , piecewise_rate_flag(0)
    , seamless_splice_flag(0)
    , ltw_valid_flag(0)
    , ltw_offset(0)
    , piecewise_rate(0)
    , splice_type(0)
    , DTS_next_AU0(0)
    , marker_bit0(0)
    , DTS_next_AU1(0)
    , marker_bit1(0)
    , DTS_next_AU2(0)
    , marker_bit2(0)
    , nb_af_ext_reserved(0)
    , nb_af_reserved(0) {
}

TsAdaptationField::~TsAdaptationField() {
    delete [] transport_private_data;
}

size_t TsAdaptationField::ByteSize() const {
    size_t sz = 2;
    if (PCR_flag) { sz += 6; }
    if (OPCR_flag) { sz += 6; }
    if (splicing_point_flag) { ++sz; }
    if (transport_private_data_flag) {
        sz += 1 + transport_private_data_length;
    }
    if (adaptation_field_extension_flag) {
        sz += 2 + adaptation_field_extension_length;
    }
    sz += nb_af_ext_reserved;
    sz += nb_af_reserved;
    return sz;
}

int TsAdaptationField::Encode(
    void* data, TsAdaptationFieldType adaptation_field_control) const {
    char* p = (char*)data;
    const size_t af_length = adaptation_field_length();
    policy::Write1Byte(&p, af_length);

    if (adaptation_field_control == TS_AF_BOTH) {
        if (af_length > 182) {
            LOG(ERROR) << "Invalid af_length=" << af_length;
            return -1;
        }
    } else if (adaptation_field_control == TS_AF_ADAPTATION_ONLY) {
        if (af_length != 183) {
            LOG(ERROR) << "Invalid af_length=" << af_length;
            return -1;
        }
    }
    // no adaptation field.
    if (af_length == 0) {
        return 0;
    }
    int8_t tmpv = adaptation_field_extension_flag & 0x01;
    tmpv |= (discontinuity_indicator << 7) & 0x80;
    tmpv |= (random_access_indicator << 6) & 0x40;
    tmpv |= (elementary_stream_priority_indicator << 5) & 0x20;
    tmpv |= (PCR_flag << 4) & 0x10;
    tmpv |= (OPCR_flag << 3) & 0x08;
    tmpv |= (splicing_point_flag << 2) & 0x04;
    tmpv |= (transport_private_data_flag << 1) & 0x02;
    policy::Write1Byte(&p, tmpv);
    
    if (PCR_flag) {
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        int64_t pcrv = program_clock_reference_extension & 0x1ff;
        pcrv |= (const1_value0 << 9) & 0x7E00;
        pcrv |= (program_clock_reference_base << 15) & 0x1FFFFFFFF000000LL;

        const char* pp = (const char*)&pcrv;
        *p++ = pp[5];
        *p++ = pp[4];
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    if (OPCR_flag) { p += 6; } // Ignore OPCR
    if (splicing_point_flag) {
        policy::Write1Byte(&p, splice_countdown);
    }
    if (transport_private_data_flag) {
        policy::Write1Byte(&p, transport_private_data_length);
        if (transport_private_data_length > 0) {
            memcpy(p, transport_private_data, transport_private_data_length);
            p += transport_private_data_length;
        }
    }
    if (adaptation_field_extension_flag) {
        policy::Write1Byte(&p, adaptation_field_extension_length);
        int8_t ltwfv = const1_value1 & 0x1F;
        ltwfv |= (ltw_flag << 7) & 0x80;
        ltwfv |= (piecewise_rate_flag << 6) & 0x40;
        ltwfv |= (seamless_splice_flag << 5) & 0x20;
        policy::Write1Byte(&p, ltwfv);
        const char* const saved_p = p;
        if (ltw_flag) { p += 2; } // Ignore ltw
        if (piecewise_rate_flag) { p += 3; } // Ignore piecewise_rate
        if (seamless_splice_flag) { p += 5; } // Ignore seamless_splice
        p += nb_af_ext_reserved;
        if (adaptation_field_extension_length != p - saved_p) {
            LOG(ERROR) << "af_extension_length="
                       << adaptation_field_extension_length
                       << " does not match other fields";
            return -1;
        }
    }
    p += nb_af_reserved;
    return 0;
}

TsPayload::TsPayload(const TsPacket* p) : _packet(p) {}
TsPayload::~TsPayload() {}

TsPayloadPES::TsPayloadPES(const TsPacket* p) : TsPayload(p) {
    _PES_header_data_length = -1;
    PES_private_data = NULL;
    pack_field = NULL;
    PES_extension_field = NULL;
    nb_stuffings = 0;
    nb_bytes = 0;
    nb_paddings = 0;
}

TsPayloadPES::~TsPayloadPES() {
    delete[] PES_private_data;
    delete[] pack_field;
    delete[] PES_extension_field;
}

size_t TsPayloadPES::ByteSize() const {
    size_t sz = 0;
    
    _PES_header_data_length = 0;
    const TsPESStreamId sid = stream_id;

    if (sid != TS_PES_STREAM_ID_PROGRAM_STREAM_MAP
        && sid != TS_PES_STREAM_ID_PADDING_STREAM
        && sid != TS_PES_STREAM_ID_PRIVATE_STREAM2
        && sid != TS_PES_STREAM_ID_ECM_STREAM
        && sid != TS_PES_STREAM_ID_EMM_STREAM
        && sid != TS_PES_STREAM_ID_PROGRAM_STREAM_DIRECTORY
        && sid != TS_PES_STREAM_ID_DSMC_STREAM
        && sid != TS_PES_STREAM_ID_H2221TYPE_E) {
        sz += 6;
        sz += 3;
        const size_t old_sz = sz;

        if (PTS_DTS_flags == 0x2) { sz += 5; }
        else if (PTS_DTS_flags == 0x3) { sz += 10; }
        
        if (ESCR_flag) { sz += 6; }
        if (ES_rate_flag) { sz += 3; }
        if (DSM_trick_mode_flag) { sz += 1; }
        if (additional_copy_info_flag) { sz += 1; }
        if (PES_CRC_flag) { sz += 2; }
        if (PES_extension_flag) {
            sz += 1;
            if (PES_private_data_flag) { sz += 16; }
            if (pack_header_field_flag) { sz += 1 + pack_field_length; }
            if (program_packet_sequence_counter_flag) { sz += 2; }
            if (P_STD_buffer_flag) { sz += 2; }
            if (PES_extension_flag_2) { sz += 1 + PES_extension_field_length; }
        }
        _PES_header_data_length = sz - old_sz;

        sz += nb_stuffings;
        // packet bytes
    } else if (sid == TS_PES_STREAM_ID_PROGRAM_STREAM_MAP
               || sid == TS_PES_STREAM_ID_PRIVATE_STREAM2
               || sid == TS_PES_STREAM_ID_ECM_STREAM
               || sid == TS_PES_STREAM_ID_EMM_STREAM
               || sid == TS_PES_STREAM_ID_PROGRAM_STREAM_DIRECTORY
               || sid == TS_PES_STREAM_ID_DSMC_STREAM
               || sid == TS_PES_STREAM_ID_H2221TYPE_E
        ) {
        // packet bytes
    } else {
        // nb_drop
    }
    return sz;
}

int TsPayloadPES::Encode(void* data) const {
    if (_PES_header_data_length < 0) {
        (void)ByteSize();
        CHECK_GE(_PES_header_data_length, 0);
    }
    char* p = (char*)data;

    // 3B
    // Together with the stream_id that follows it constitutes a packet start
    // code that identifies the beginning of a packet.
    policy::WriteBigEndian3Bytes(&p, 0x1);
    // 1B
    policy::Write1Byte(&p, stream_id);
    // 2B
    // the PES_packet_length is the actual bytes size, the pplv write to ts
    // is the actual bytes plus the header size.
    int32_t pplv = 0;
    if (PES_packet_length > 0) {
        pplv = PES_packet_length + 3 + _PES_header_data_length;
        pplv = (pplv > 0xFFFF)? 0 : pplv;
    }
    policy::WriteBigEndian2Bytes(&p, pplv);
    
    int8_t oocv = original_or_copy & 0x01;
    oocv |= (const2bits << 6) & 0xC0;
    oocv |= (PES_scrambling_control << 4) & 0x30;
    oocv |= (PES_priority << 3) & 0x08;
    oocv |= (data_alignment_indicator << 2) & 0x04;
    oocv |= (copyright << 1) & 0x02;
    policy::Write1Byte(&p, oocv);
    
    int8_t pefv = PES_extension_flag & 0x01;
    pefv |= (PTS_DTS_flags << 6) & 0xC0;
    pefv |= (ESCR_flag << 5) & 0x20;
    pefv |= (ES_rate_flag << 4) & 0x10;
    pefv |= (DSM_trick_mode_flag << 3) & 0x08;
    pefv |= (additional_copy_info_flag << 2) & 0x04;
    pefv |= (PES_CRC_flag << 1) & 0x02;
    policy::Write1Byte(&p, pefv);
    policy::Write1Byte(&p, _PES_header_data_length);
    
    if (PTS_DTS_flags == 0x2) { // 5B
        encode_33bits_dts_pts(&p, 0x02, pts);
    } else if (PTS_DTS_flags == 0x3) { // 10B
        encode_33bits_dts_pts(&p, 0x03, pts);
        encode_33bits_dts_pts(&p, 0x01, dts);
        // the diff of dts and pts should never be greater than 1s.
        if (labs(dts - pts) > 90000) {
            LOG(WARNING) << "Diff between dts=" << dts << " and pts=" << pts
                         << " is greater than 1 second";
        }
    }
    if (ESCR_flag) { p += 6; }
    if (ES_rate_flag) { p += 3; }
    if (DSM_trick_mode_flag) { p += 1; }
    if (additional_copy_info_flag) { p += 1; }
    if (PES_CRC_flag) { p += 2; }
    if (PES_extension_flag) {
        int8_t efv = PES_extension_flag_2 & 0x01;
        efv |= (PES_private_data_flag << 7) & 0x80;
        efv |= (pack_header_field_flag << 6) & 0x40;
        efv |= (program_packet_sequence_counter_flag << 5) & 0x20;
        efv |= (P_STD_buffer_flag << 4) & 0x10;
        efv |= (const1_value0 << 1) & 0xE0;
        policy::Write1Byte(&p, efv);

        if (PES_private_data_flag) { p += 16; }
        if (pack_header_field_flag) { p += 1 + pack_field_length; }
        if (program_packet_sequence_counter_flag) { p += 2; }
        if (P_STD_buffer_flag) { p += 2; }
        if (PES_extension_flag_2) { p += 1 + PES_extension_field_length; }
    }
    // stuffing_byte
    p += nb_stuffings;
    return 0;
}

void TsPayloadPES::encode_33bits_dts_pts(
    char** data, uint8_t fb, int64_t v) const {
    char* p = *data;
    int32_t val = (fb << 4) | (((v >> 30) & 0x07) << 1) | 1;
    *p++ = val;
    
    val = (((v >> 15) & 0x7fff) << 1) | 1;
    *p++ = (val >> 8);
    *p++ = val;
    
    val = (((v) & 0x7fff) << 1) | 1;
    *p++ = (val >> 8);
    *p++ = val;

    *data = p;
}

TsPayloadPSI::TsPayloadPSI(const TsPacket* p)
    : TsPayload(p)
    , _section_length(-1)
    , pointer_field(0)
    , section_syntax_indicator(1)
    , table_id(TS_PSI_ID_PAS) {
}

TsPayloadPSI::~TsPayloadPSI() {}

size_t TsPayloadPSI::ByteSize() const {
    size_t sz = 0;

    // section size is the sl plus the crc32
    _section_length = PsiByteSize() + 4;
    if (packet()->payload_unit_start_indicator()) { sz += 1; }
    sz += 3;
    sz += _section_length;
    return sz;
}

int TsPayloadPSI::Encode(void* data) const {
    char* p = (char*)data;
    if (_section_length < 0) {
        (void)ByteSize();
        CHECK_GE(_section_length, 0);
    }
    if (packet()->payload_unit_start_indicator()) {
        policy::Write1Byte(&p, pointer_field);
    }
    const char* const section_start = p;    // to calc the crc32
    policy::Write1Byte(&p, table_id);
    int16_t slv = _section_length & 0x0FFF;
    slv |= (section_syntax_indicator << 15) & 0x8000;
    slv |= (const0_value << 14) & 0x4000;
    slv |= (const1_value << 12) & 0x3000;
    policy::WriteBigEndian2Bytes(&p, slv);
    if (_section_length == 0) {
        return 0;
    }
    if (PsiEncode(p) != 0) {
        LOG(ERROR) << "Fail to TsPayloadPSI.PsiEncode";
        return -1;
    }
    p += _section_length - 4;
    const uint32_t crc32 = mpegts_crc32(section_start, p - section_start);
    policy::WriteBigEndian4Bytes(&p, crc32);
    return 0;
}

TsPayloadPATProgram::TsPayloadPATProgram(int16_t number, TsPid pid2)
    : program_number(number)
    , pid(pid2) {
}

TsPayloadPATProgram::~TsPayloadPATProgram() {}

int TsPayloadPATProgram::Encode(void* data) const {
    char* p = (char*)data;
    int tmpv = pid & 0x1FFF;
    tmpv |= (program_number << 16) & 0xFFFF0000;
    tmpv |= (const1_value << 13) & 0xE000;
    policy::WriteBigEndian4Bytes(&p, tmpv);
    return 0;
}

TsPayloadPAT::TsPayloadPAT(const TsPacket* p)
    : TsPayloadPSI(p)
    , transport_stream_id(0)
    , version_number(0)
    , current_next_indicator(0)
    , section_number(0)
    , last_section_number(0) {
}

TsPayloadPAT::~TsPayloadPAT() {}

size_t TsPayloadPAT::PsiByteSize() const {
    size_t sz = 5;
    for (size_t i = 0; i < programs.size(); ++i) {
        sz += programs[i].ByteSize();
    }
    return sz;
}

int TsPayloadPAT::PsiEncode(void* data) const {
    char*  p = (char*)data;

    policy::WriteBigEndian2Bytes(&p, transport_stream_id);
    
    int8_t cniv = current_next_indicator & 0x01;
    cniv |= (version_number << 1) & 0x3E;
    cniv |= (const1_value << 6) & 0xC0;
    policy::Write1Byte(&p, cniv);
    
    policy::Write1Byte(&p, section_number);
    policy::Write1Byte(&p, last_section_number);

    for (size_t i = 0; i < programs.size(); ++i) {
        if (programs[i].Encode(p) != 0) {
            LOG(ERROR) << "Fail to encode TsPayloadPAT.programs[" << i << ']';
            return -1;
        }
        p += programs[i].ByteSize();
        // update the apply pid table.
        packet()->channel_group()->set(programs[i].pid);
    }

    // update the apply pid table.
    packet()->channel_group()->set(packet()->pid());
    return 0;
}

TsPayloadPMTESInfo::TsPayloadPMTESInfo(TsStream st, TsPid epid)
    : stream_type(st)
    , elementary_PID(epid)
    , ES_info_length(0)
    , ES_info(NULL) {
}

TsPayloadPMTESInfo::~TsPayloadPMTESInfo() {
    delete [] ES_info;
}

size_t TsPayloadPMTESInfo::ByteSize() const {
    return 5 + ES_info_length;
}

int TsPayloadPMTESInfo::Encode(void* data) const {
    char* p = (char*)data;

    policy::Write1Byte(&p, stream_type);

    int16_t epv = elementary_PID & 0x1FFF;
    epv |= (const1_value0 << 13) & 0xE000;
    policy::WriteBigEndian2Bytes(&p, epv);
    
    int16_t eilv = ES_info_length & 0x0FFF;
    eilv |= (const1_value1 << 12) & 0xF000;
    policy::WriteBigEndian2Bytes(&p, eilv);

    if (ES_info_length > 0) {
        memcpy(p, ES_info, ES_info_length);
        p += ES_info_length;
    }
    return 0;
}

TsPayloadPMT::TsPayloadPMT(const TsPacket* p)
    : TsPayloadPSI(p)
    , program_info_length(0)
    , program_info_desc(NULL) {
}

TsPayloadPMT::~TsPayloadPMT() {
    delete [] program_info_desc;

    for (std::vector<TsPayloadPMTESInfo*>::iterator it = infos.begin();
         it != infos.end(); ++it) {
        delete *it;
    }
    infos.clear();
}

size_t TsPayloadPMT::PsiByteSize() const {
    size_t sz = 9;
    sz += program_info_length;
    for (size_t i = 0; i < infos.size(); ++i) {
        sz += infos[i]->ByteSize();
    }
    return sz;
}

int TsPayloadPMT::PsiEncode(void* data) const {
    char* p = (char*)data;

    policy::WriteBigEndian2Bytes(&p, program_number);
    
    int8_t cniv = current_next_indicator & 0x01;
    cniv |= (const1_value0 << 6) & 0xC0;
    cniv |= (version_number << 1) & 0xFE;
    policy::Write1Byte(&p, cniv);
    
    policy::Write1Byte(&p, section_number);
    policy::Write1Byte(&p, last_section_number);

    int16_t ppv = PCR_PID & 0x1FFF;
    ppv |= (const1_value1 << 13) & 0xE000;
    policy::WriteBigEndian2Bytes(&p, ppv);
    
    int16_t pilv = program_info_length & 0xFFF;
    pilv |= (const1_value2 << 12) & 0xF000;
    policy::WriteBigEndian2Bytes(&p, pilv);

    if (program_info_length > 0) {
        memcpy(p, program_info_desc, program_info_length);
        p += program_info_length;
    }

    for (size_t i = 0; i < infos.size(); ++i) {
        TsPayloadPMTESInfo* info = infos[i];
        if (info->Encode(p) != 0) {
            LOG(ERROR) << "Fail to encode TsPayloadPMT.infos[" << i << ']';
            return -1;
        }
        p += info->ByteSize();

        // update the apply pid table
        switch (info->stream_type) {
        case TS_STREAM_VIDEO_H264:
        case TS_STREAM_VIDEO_MPEG4:
            packet()->channel_group()->set(info->elementary_PID);
            break;
        case TS_STREAM_AUDIO_AAC:
        case TS_STREAM_AUDIO_AC3:
        case TS_STREAM_AUDIO_DTS:
        case TS_STREAM_AUDIO_MP3:
            packet()->channel_group()->set(info->elementary_PID);
            break;
        default:
            LOG(WARNING) << "Drop pid=" << info->elementary_PID
                         << " stream=" << info->stream_type;
            break;
        }
    }

    // update the apply pid table.
    packet()->channel_group()->set(packet()->pid());
    return 0;
}

TsStream FlvVideoCodec2TsStream(FlvVideoCodec codec, TsPid* pid) {
    switch (codec) {
    case FLV_VIDEO_AVC:
        if (pid) {
            *pid = TS_PID_VIDEO_AVC;
        }
        return TS_STREAM_VIDEO_H264; 
    case FLV_VIDEO_JPEG:
    case FLV_VIDEO_SORENSON_H263:
    case FLV_VIDEO_SCREEN_VIDEO:
    case FLV_VIDEO_ON2_VP6:
    case FLV_VIDEO_ON2_VP6_WITH_ALPHA_CHANNEL:
    case FLV_VIDEO_SCREEN_VIDEO_V2:
    case FLV_VIDEO_HEVC:
        return TS_STREAM_RESERVED;
    }
    return TS_STREAM_RESERVED;
}

TsStream FlvAudioCodec2TsStream(FlvAudioCodec codec, TsPid* pid) {
    switch (codec) {
    case FLV_AUDIO_AAC:
        if (pid) {
            *pid = TS_PID_AUDIO_AAC;
        }
        return TS_STREAM_AUDIO_AAC; 
    case FLV_AUDIO_MP3:
        if (pid) {
            *pid = TS_PID_AUDIO_MP3;
        }  
        return TS_STREAM_AUDIO_MP3; 
    case FLV_AUDIO_LINEAR_PCM_PLATFORM_ENDIAN:
    case FLV_AUDIO_ADPCM:
    case FLV_AUDIO_LINEAR_PCM_LITTLE_ENDIAN:
    case FLV_AUDIO_NELLYMOSER_16KHZ_MONO:
    case FLV_AUDIO_NELLYMOSER_8KHZ_MONO:
    case FLV_AUDIO_NELLYMOSER:
    case FLV_AUDIO_G711_ALAW_LOGARITHMIC_PCM:
    case FLV_AUDIO_G711_MULAW_LOGARITHMIC_PCM:
    case FLV_AUDIO_RESERVED:
    case FLV_AUDIO_SPEEX:
    case FLV_AUDIO_MP3_8KHZ:
    case FLV_AUDIO_DEVICE_SPECIFIC_SOUND:
        return TS_STREAM_RESERVED;
    }
    return TS_STREAM_RESERVED;
}

AACProfile AACObjectType2Profile(AACObjectType object_type) {
    switch (object_type) {
    case AAC_OBJECT_MAIN:
        return AAC_PROFILE_MAIN;
    case AAC_OBJECT_HE:
    case AAC_OBJECT_HEV2:
    case AAC_OBJECT_LC:
        return AAC_PROFILE_LC;
    case AAC_OBJECT_SSR:
        return AAC_PROFILE_SSR;
    }
    return AAC_PROFILE_UNKNOWN;
}

TsWriter::TsWriter(butil::IOBuf* outbuf)
    : _outbuf(outbuf)
    , _nalu_format(AVC_NALU_FORMAT_UNKNOWN)
    , _has_avc_seq_header(false)
    , _has_aac_seq_header(false)
    , _encoded_pat_pmt(false)
    , _last_video_stream(TS_STREAM_VIDEO_H264)
    , _last_video_pid(TS_PID_VIDEO_AVC)
    , _last_audio_stream(TS_STREAM_AUDIO_AAC)
    , _last_audio_pid(TS_PID_AUDIO_AAC)
    , _discontinuity_counter(0) {
}

TsWriter::~TsWriter() {
}

butil::Status TsWriter::Write(const RtmpAudioMessage& msg) {
    // ts support audio codec: aac/mp3
    if (msg.codec != FLV_AUDIO_AAC && msg.codec != FLV_AUDIO_MP3) {
        return butil::Status(EINVAL, "Unsupported codec=%s",
                            FlvAudioCodec2Str(msg.codec));
    }
    const int64_t dts = static_cast<int64_t>(msg.timestamp) * 90;
    TsMessage tsmsg;
    tsmsg.write_pcr = false;
    tsmsg.dts = dts;
    tsmsg.pts = dts;
    tsmsg.sid = TS_PES_STREAM_ID_AUDIO_COMMON;

    if (msg.codec == FLV_AUDIO_AAC) {
        RtmpAACMessage aac_msg;
        butil::Status st = aac_msg.Create(msg);
        if (!st.ok()) {
            return st;
        }
        // ignore sequence header
        if (aac_msg.packet_type == FLV_AAC_PACKET_SEQUENCE_HEADER) {
            butil::Status st2 = _aac_seq_header.Create(aac_msg.data);
            if (!st2.ok()) {
                return st2;
            }
            _has_aac_seq_header = true;
            ++_discontinuity_counter;
            return butil::Status::OK();
        }
        if (!_has_aac_seq_header) {
            return butil::Status(EINVAL, "Lack of AAC sequence header");
        }
        if (aac_msg.data.size() > 0x1fff) {
            return butil::Status(EINVAL, "Invalid AAC data_size=%" PRIu64,
                                (uint64_t)aac_msg.data.size());
        }
        
        // the frame length is the AAC raw data plus the adts header size.
        const int32_t frame_length = aac_msg.data.size() + 7;
        
        // AAC-ADTS
        // 6.2 Audio Data Transport Stream, ADTS
        // in aac-iso-13818-7.pdf, page 26.
        // fixed 7bytes header
        uint8_t adts_header[7] = {0xff, 0xf9, 0x00, 0x00, 0x00, 0x0f, 0xfc};
        // profile, 2bits
        const AACProfile aac_profile =
            AACObjectType2Profile(_aac_seq_header.aac_object);
        if (aac_profile == AAC_PROFILE_UNKNOWN) {
            return butil::Status(EINVAL, "Invalid aac_object=%d",
                                (int)_aac_seq_header.aac_object);
        }
        adts_header[2] = (aac_profile << 6) & 0xc0;
        // sampling_frequency_index 4bits
        adts_header[2] |= (_aac_seq_header.aac_sample_rate << 2) & 0x3c;
        // channel_configuration 3bits
        adts_header[2] |= (_aac_seq_header.aac_channels >> 2) & 0x01;
        adts_header[3] = (_aac_seq_header.aac_channels << 6) & 0xc0;
        // frame_length 13bits
        adts_header[3] |= (frame_length >> 11) & 0x03;
        adts_header[4] = (frame_length >> 3) & 0xff;
        adts_header[5] = ((frame_length << 5) & 0xe0);
        // adts_buffer_fullness; //11bits
        adts_header[5] |= 0x1f;

        tsmsg.payload.append(adts_header, sizeof(adts_header));
        tsmsg.payload.append(aac_msg.data);
    } else if (msg.codec == FLV_AUDIO_MP3) {
        tsmsg.payload.append(msg.data);
    }

    TsPid apid = TS_PID_NULL;
    TsStream as = FlvAudioCodec2TsStream(msg.codec, &apid);
    if (as == TS_STREAM_RESERVED) {
        return butil::Status(EINVAL, "Unsupported audio codec=%s",
                            FlvAudioCodec2Str(msg.codec));
    }
    return Encode(&tsmsg, as, apid);
}

// mux the samples in annexb format,
// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 324.
// 00 00 00 01 // header
//     xxxxxxx // data bytes
// 00 00 01    // continue header
//     xxxxxxx // data bytes.
//
// nal_unit_type specifies the type of RBSP data structure contained in
// the NAL unit as specified in Table 7-1.
// Table 7-1 - NAL unit type codes, syntax element categories, and NAL
// unit type classes. H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
//      1, Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
//      2, Coded slice data partition A slice_data_partition_a_layer_rbsp( )
//      3, Coded slice data partition B slice_data_partition_b_layer_rbsp( )
//      4, Coded slice data partition C slice_data_partition_c_layer_rbsp( )
//      5, Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
//      6, Supplemental enhancement information (SEI) sei_rbsp( )
//      7, Sequence parameter set seq_parameter_set_rbsp( )
//      8, Picture parameter set pic_parameter_set_rbsp( )
//      9, Access unit delimiter access_unit_delimiter_rbsp( )
//      10, End of sequence end_of_seq_rbsp( )
//      11, End of stream end_of_stream_rbsp( )
//      12, Filler data filler_data_rbsp( )
//      13, Sequence parameter set extension seq_parameter_set_extension_rbsp( )
//      14, Prefix NAL unit prefix_nal_unit_rbsp( )
//      15, Subset sequence parameter set subset_seq_parameter_set_rbsp( )
//      19, Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
//      20, Coded slice extension slice_layer_extension_rbsp( )
// the first ts message of apple sample:
//      annexb 4B header, 2B aud(nal_unit_type:6)(0x09 0xf0)
//      annexb 4B header, 19B sps(nal_unit_type:7)
//      annexb 3B header, 4B pps(nal_unit_type:8)
//      annexb 3B header, 12B nalu(nal_unit_type:6)
//      annexb 3B header, 21B nalu(nal_unit_type:6)
//      annexb 3B header, 2762B nalu(nal_unit_type:5)
//      annexb 3B header, 3535B nalu(nal_unit_type:5)
// the second ts message of apple ts sample:
//      annexb 4B header, 2B aud(nal_unit_type:6)(0x09 0xf0)
//      annexb 3B header, 21B nalu(nal_unit_type:6)
//      annexb 3B header, 379B nalu(nal_unit_type:1)
//      annexb 3B header, 406B nalu(nal_unit_type:1)
static const uint8_t fresh_nalu_header[] = { 0x00, 0x00, 0x00, 0x01 };
static const uint8_t cont_nalu_header[] = { 0x00, 0x00, 0x01 };
    
// the aud(access unit delimiter) before each frame.
// 7.3.2.4 Access unit delimiter RBSP syntax
// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 66.
// primary_pic_type u(3), the first 3bits, primary_pic_type indicates
// that the slice_type values for all slices of the primary coded picture
// are members of the set listed in Table 7-5 for the given value of
// primary_pic_type.
//      0, slice_type 2, 7
//      1, slice_type 0, 2, 5, 7
//      2, slice_type 0, 1, 2, 5, 6, 7
//      3, slice_type 4, 9
//      4, slice_type 3, 4, 8, 9
//      5, slice_type 2, 4, 7, 9
//      6, slice_type 0, 2, 3, 4, 5, 7, 8, 9
//      7, slice_type 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
// 7.4.2.4 Access unit delimiter RBSP semantics
// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 102.
//
// slice_type specifies the coding type of the slice according to Table 7-6.
//      0, P (P slice)
//      1, B (B slice)
//      2, I (I slice)
//      3, SP (SP slice)
//      4, SI (SI slice)
//      5, P (P slice)
//      6, B (B slice)
//      7, I (I slice)
//      8, SP (SP slice)
//      9, SI (SI slice)
// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 105.
//static const uint8_t aud_nalu_7[] = { 0x09, 0xf0};
static const uint8_t fresh_nalu_header_and_aud_nalu_7[] =
{ 0x00, 0x00, 0x00, 0x01, 0x09, 0xf0};

butil::Status TsWriter::Write(const RtmpVideoMessage& msg) {
    if (msg.frame_type == FLV_VIDEO_FRAME_INFOFRAME) {
        // Ignore info frame.
        return butil::Status::OK();
    }
    if (msg.codec != FLV_VIDEO_AVC) {
        return butil::Status(EINVAL, "video_codec=%s is not AVC",
                            FlvVideoCodec2Str(msg.codec));
    }
    RtmpAVCMessage avc_msg;
    butil::Status st = avc_msg.Create(msg);
    if (!st.ok()) {
        return st;
    }
    // ignore sequence header
    if (avc_msg.frame_type == FLV_VIDEO_FRAME_KEYFRAME &&
        avc_msg.packet_type == FLV_AVC_PACKET_SEQUENCE_HEADER) {
        butil::Status st2 = _avc_seq_header.Create(avc_msg.data);
        if (!st2.ok()) {
            return st2;
        }
        _has_avc_seq_header = true;
        ++_discontinuity_counter;
        return butil::Status::OK();
    }
    if (!_has_avc_seq_header) {
        return butil::Status(EINVAL, "Lack of AVC sequence header");
    }

    const int64_t dts = static_cast<int64_t>(avc_msg.timestamp) * 90;
    TsMessage tsmsg;
    tsmsg.write_pcr = (msg.frame_type == FLV_VIDEO_FRAME_KEYFRAME);
    tsmsg.dts = dts;
    tsmsg.pts = dts + static_cast<int64_t>(avc_msg.composition_time) * 90;
    tsmsg.sid = TS_PES_STREAM_ID_VIDEO_COMMON;

    // always append a aud nalu for each frame.
    tsmsg.payload.append(fresh_nalu_header_and_aud_nalu_7,
                         arraysize(fresh_nalu_header_and_aud_nalu_7));
    butil::IOBuf nalus;
    bool has_idr = false;
    for (AVCNaluIterator it(&avc_msg.data, _avc_seq_header.length_size_minus1,
                            &_nalu_format); it != NULL; ++it) {
        // ignore SPS/PPS/AUD
        switch (it.nalu_type()) {
        case AVC_NALU_IDR:
            has_idr = true;
            break;
        case AVC_NALU_SPS:
        case AVC_NALU_PPS:
        case AVC_NALU_ACCESSUNITDELIMITER:
            continue;
        default:
            break;
        }
        
        // append cont nalu header before NAL units.
        nalus.append(cont_nalu_header, 3);
        nalus.append(*it);
    }
    
    // when ts message(samples) contains IDR, insert sps+pps.
    if (has_idr) {
        bool first = true;
        for (size_t i = 0; i < _avc_seq_header.sps_list.size(); ++i) {
            if (first) {
                tsmsg.payload.append(fresh_nalu_header, arraysize(fresh_nalu_header));
                first = false;
            } else {
                tsmsg.payload.append(cont_nalu_header, arraysize(cont_nalu_header));
            }
            tsmsg.payload.append(_avc_seq_header.sps_list[i]);
            RPC_VLOG << "Append sps[" << i << "]=" << _avc_seq_header.sps_list[i].size();
        }
        for (size_t i = 0; i < _avc_seq_header.pps_list.size(); ++i) {
            if (first) {
                tsmsg.payload.append(fresh_nalu_header, arraysize(fresh_nalu_header));
                first = false;
            } else {
                tsmsg.payload.append(cont_nalu_header, arraysize(cont_nalu_header));
            }
            tsmsg.payload.append(_avc_seq_header.pps_list[i]);
            RPC_VLOG << "Append pps[" << i << "]=" << _avc_seq_header.pps_list[i].size();
        }
    }
    tsmsg.payload.append(nalus);

    TsPid vpid = TS_PID_PAT;
    TsStream vs = FlvVideoCodec2TsStream(msg.codec, &vpid);
    if (vs == TS_STREAM_RESERVED) {
        return butil::Status(EINVAL, "Unsupported video codec=%s",
                            FlvVideoCodec2Str(msg.codec));
    }
    return Encode(&tsmsg, vs, vpid);
}

butil::Status
TsWriter::EncodePATPMT(TsStream vs, TsPid vpid, TsStream as, TsPid apid) {
    char buf[TS_PACKET_SIZE];
    
    TsPacket pat(&_tschan_group);
    pat.CreateAsPAT(TS_PMT_NUMBER, TS_PID_PMT);
    // set the left bytes with 0xFF.
    const size_t size1 = pat.ByteSize();
    CHECK_LT(size1, TS_PACKET_SIZE);
    memset(buf, 0xFF, TS_PACKET_SIZE);
    if (pat.Encode(buf) != 0) {
        return butil::Status(EINVAL, "Fail to encode PAT");
    }
    _outbuf->append(buf, TS_PACKET_SIZE);
    
    TsPacket pmt(&_tschan_group);
    if (pmt.CreateAsPMT(TS_PMT_NUMBER, TS_PID_PMT, vpid, vs, apid, as) != 0) {
        return butil::Status(EINVAL, "Fail to CreateAsPMT");
    }
    // set the left bytes with 0xFF.
    const size_t size2 = pmt.ByteSize();
    CHECK_LT(size2, TS_PACKET_SIZE);
    memset(buf, 0xFF, TS_PACKET_SIZE);
    if (pmt.Encode(buf) != 0) {
        return butil::Status(EINVAL, "Fail to encode PMT");
    }
    _outbuf->append(buf, TS_PACKET_SIZE);
    return butil::Status::OK();
}

butil::Status TsWriter::Encode(TsMessage* msg, TsStream stream, TsPid pid) {
    if (stream == TS_STREAM_RESERVED) {
        return butil::Status(EINVAL, "Invalid stream=%d", (int)stream);
    }
    // Encode the media frame to PES packets over TS.
    bool add_pat_pmt = false;
    if (is_audio(msg->sid)) {
        if (stream != _last_audio_stream) {
            _last_audio_stream = stream;
            _last_audio_pid = pid;
            add_pat_pmt = true;
        }
    } else if (is_video(msg->sid)) {
        if (stream != _last_video_stream) {
            _last_video_stream = stream;
            _last_video_pid = pid;
            add_pat_pmt = true;
        }
    } else {
        return butil::Status(EINVAL, "Unknown stream_id=%d", (int)msg->sid);
    }
    if (!_encoded_pat_pmt) {
        _encoded_pat_pmt = true;
        add_pat_pmt = true;
    }
    if (add_pat_pmt) {
        butil::Status st = EncodePATPMT(_last_video_stream, _last_video_pid,
                                       _last_audio_stream, _last_audio_pid);
        if (!st.ok()) {
            return st;
        }
    }
    return EncodePES(msg, stream, pid,
                     (_last_video_stream == TS_STREAM_RESERVED));
}

butil::Status TsWriter::EncodePES(TsMessage* msg, TsStream sid, TsPid pid,
                                 bool pure_audio) {
    if (msg->payload.empty()) {
        return butil::Status::OK();
    }
    if (sid != TS_STREAM_VIDEO_H264 &&
        sid != TS_STREAM_AUDIO_MP3 &&
        sid != TS_STREAM_AUDIO_AAC) {
        LOG(WARNING) << "Ignore unknown stream_id=" << sid;
        return butil::Status::OK();
    }

    TsChannel* channel = _tschan_group.get(pid);
    if (channel == NULL) {
        return butil::Status(EINVAL, "Fail to get channel on pid=%d", (int)pid);
    }

    bool first_msg = true;
    while (!msg->payload.empty()) {
        TsPacket pkt(&_tschan_group);
        if (first_msg) {
            first_msg = false;
            bool write_pcr = msg->write_pcr;
            // for pure audio, always write pcr.
            // TODO: maybe only need to write at begin and end of ts.
            if (pure_audio && is_audio(msg->sid)) {
                write_pcr = true;
            }

            // it's ok to set pcr equals to dts,
            int64_t pcr = write_pcr ? msg->dts : -1;
            
            // TODO: FIXME: figure out why use discontinuity of msg
            pkt.CreateAsPESFirst(pid, msg->sid, channel->continuity_counter++,
                               msg->is_discontinuity, pcr, msg->dts,
                               msg->pts, msg->payload.size());
        } else {
            pkt.CreateAsPESContinue(pid, channel->continuity_counter++);
        }

        char buf[TS_PACKET_SIZE];

        // set the left bytes with 0xFF.
        size_t pkt_size = pkt.ByteSize();
        CHECK_LT(pkt_size, TS_PACKET_SIZE);

        size_t left = std::min(msg->payload.size(), TS_PACKET_SIZE - pkt_size);
        const size_t nb_stuffings = TS_PACKET_SIZE - pkt_size - left;
        if (nb_stuffings > 0) {
            // set all bytes to stuffings.
            memset(buf, 0xFF, TS_PACKET_SIZE);

            pkt.AddPadding(nb_stuffings);

            pkt_size = pkt.ByteSize();   // size changed, recalculate.
            CHECK_LT(pkt_size, TS_PACKET_SIZE);

            left = std::min(msg->payload.size(), TS_PACKET_SIZE - pkt_size);
            if (TS_PACKET_SIZE != pkt_size + left) {
                LOG(ERROR) << "pkt_size=" << pkt_size << " left=" << left
                           << " stuffing=" << nb_stuffings << " payload="
                           << msg->payload.size();
            }
        }
        msg->payload.cutn(buf + pkt_size, left);
        if (pkt.Encode(buf) != 0) {
            return butil::Status(EINVAL, "Fail to encode PES");
        }
        _outbuf->append(buf, TS_PACKET_SIZE);
    }
    return butil::Status::OK();
}

} // namespace brpc
