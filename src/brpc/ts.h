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

#ifndef BRPC_TS_H
#define BRPC_TS_H

#include <string>
#include <map>
#include <vector>
#include <stdint.h>
#include "butil/iobuf.h"
#include "brpc/rtmp.h"


namespace brpc {

class TsAdaptationField;
class TsPayload;
class TsPacket;
class TsChannelGroup;

// The pid of ts packet (13 bits)
typedef int16_t TsPid;

// the transport_scrambling_control of ts packet,
// Table 2-4 - Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
enum TsScrambled {
    TS_SCRAMBLED_DISABLED      = 0x00,  // Not scrambled
    TS_SCRAMBLED_USERDEFINED1  = 0x01,  // User-defined
    TS_SCRAMBLED_USERDEFINED2  = 0x02,  // ^
    TS_SCRAMBLED_USERDEFINED3  = 0x03,  // ^
};

// the adaptation_field_control of ts packet,
// Table 2-5 - Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
enum TsAdaptationFieldType {
    TS_AF_RESERVED        = 0x00,  // Reserved for future use by ISO/IEC
    TS_AF_PAYLOAD_ONLY    = 0x01,  // No adaptation_field, payload only
    TS_AF_ADAPTATION_ONLY = 0x02,  // Adaptation_field only, no payload
    TS_AF_BOTH            = 0x03,  // Adaptation_field followed by payload
};

// Table 2-29 - Stream type assignments
enum TsStream {
    TS_STREAM_RESERVED    = 0x00, // ITU-T | ISO/IEC Reserved
    // 0x01: ISO/IEC 11172 Video
    // 0x02: ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2
    //       constrained parameter video stream
    // 0x03: ISO/IEC 11172 Audio
    TS_STREAM_AUDIO_MP3   = 0x04, // ISO/IEC 13818-3 Audio
    // 0x05: ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections
    // 0x06: ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing
    //       private data
    // 0x07: ISO/IEC 13522 MHEG
    // 0x08: ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC
    // 0x09: ITU-T Rec. H.222.1
    // 0x0A: ISO/IEC 13818-6 type A
    // 0x0B: ISO/IEC 13818-6 type B
    // 0x0C: ISO/IEC 13818-6 type C
    // 0x0D: ISO/IEC 13818-6 type D
    // 0x0E: ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary
    TS_STREAM_AUDIO_AAC   = 0x0F, // ISO/IEC 13818-7 Audio with ADTS transport syntax
    TS_STREAM_VIDEO_MPEG4 = 0x10, // ISO/IEC 14496-2 Visual
    TS_STREAM_AUDIO_MPEG4 = 0x11, // ISO/IEC 14496-3 Audio with the LATM transport
                                  // syntax as defined in ISO/IEC 14496-3 / AMD 1
    // 0x12: ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in
    //       PES packets
    // 0x13: ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in
    //       ISO/IEC14496_sections.
    // 0x14: ISO/IEC 13818-6 Synchronized Download Protocol
    // 0x15-0x7F: ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
    TS_STREAM_VIDEO_H264 = 0x1B,
    // 0x80-0xFF: User Private
    TS_STREAM_AUDIO_AC3  = 0x81,
    TS_STREAM_AUDIO_DTS  = 0x8A,
};
const char* TsStream2Str(TsStream stream);

// the stream_id of PES payload of ts packet.
// Table 2-18 - Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
enum TsPESStreamId {
    TS_PES_STREAM_ID_PROGRAM_STREAM_MAP       = 0xbc, // 0b10111100
    TS_PES_STREAM_ID_PRIVATE_STREAM1          = 0xbd, // 0b10111101
    TS_PES_STREAM_ID_PADDING_STREAM           = 0xbe, // 0b10111110
    TS_PES_STREAM_ID_PRIVATE_STREAM2          = 0xbf, // 0b10111111
    // 110x xxxx: ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or
    //            ISO/IEC 14496-3 audio stream number x xxxx
    TS_PES_STREAM_ID_AUDIO_COMMON             = 0xc0, // 0b11000000,
    // 1110 xxxx: ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or
    //            ISO/IEC 14496-2 video stream number xxxx
    TS_PES_STREAM_ID_VIDEO_COMMON             = 0xe0, // 0b11100000
    TS_PES_STREAM_ID_ECM_STREAM               = 0xf0, // 0b11110000
    TS_PES_STREAM_ID_EMM_STREAM               = 0xf1, // 0b11110001
    TS_PES_STREAM_ID_DSMC_STREAM              = 0xf2, // 0b11110010
    TS_PES_STREAM_ID_13522_STREAM             = 0xf3, // 0b11110011
    TS_PES_STREAM_ID_H2221TYPE_A              = 0xf4, // 0b11110100
    TS_PES_STREAM_ID_H2221TYPE_B              = 0xf5, // 0b11110101
    TS_PES_STREAM_ID_H2221TYPE_C              = 0xf6, // 0b11110110
    TS_PES_STREAM_ID_H2221TYPE_D              = 0xf7, // 0b11110111
    TS_PES_STREAM_ID_H2221TYPE_E              = 0xf8, // 0b11111000
    TS_PES_STREAM_ID_ANCILLARY_STREAM         = 0xf9, // 0b11111001
    TS_PES_STREAM_ID_SL_PACKETIZED_STREAM     = 0xfa, // 0b11111010
    TS_PES_STREAM_ID_FLEX_MUX_STREAM          = 0xfb, // 0b11111011
    // 1111 1100 ... 1111 1110 : reserved data stream
    TS_PES_STREAM_ID_PROGRAM_STREAM_DIRECTORY = 0xff, // 0b11111111
};
static const TsPESStreamId TS_PES_STREAM_ID_UNKNOWN = (TsPESStreamId)0;

// 2.4.4.4 Table_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 62
// The table_id field identifies the contents of a Transport Stream PSI
// section as shown in Table 2-26.
enum TsPsiId {
    TS_PSI_ID_PAS             = 0x00, // program_association_section
    TS_PSI_ID_CAS             = 0x01, // conditional_access_section (CA_section)
    TS_PSI_ID_PMS             = 0x02, // TS_program_map_section
    TS_PSI_ID_DS              = 0x03, // TS_description_section
    TS_PSI_ID_SDS             = 0x04, // ISO_IEC_14496_scene_description_section
    TS_PSI_ID_ODS             = 0x05, // ISO_IEC_14496_object_descriptor_section
    TS_PSI_ID_ISO138181_START = 0x06, // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 reserved
    TS_PSI_ID_ISO138181_END   = 0x37, // ^
    TS_PSI_ID_ISO138186_START = 0x38, // Defined in ISO/IEC 13818-6
    TS_PSI_ID_ISO138186_END   = 0x3F, // ^
    TS_PSI_ID_USER_START      = 0x40, // User private
    TS_PSI_ID_USER_END        = 0xFE, // ^
    TS_PSI_ID_FORBIDDEN       = 0xFF, // forbidden
};

// 7.1 Profiles, aac-iso-13818-7.pdf, page 40
enum AACProfile {
    AAC_PROFILE_MAIN = 0,
    AAC_PROFILE_LC = 1,
    AAC_PROFILE_SSR = 2,
};
static const AACProfile AAC_PROFILE_UNKNOWN = (AACProfile)4;
AACProfile AACObjectType2Profile(AACObjectType object_type);

struct TsChannel {
    uint8_t continuity_counter;
    TsChannel() : continuity_counter(0) {}
};

// Map pids to TsChannels
class TsChannelGroup {
public:
    TsChannelGroup();
    ~TsChannelGroup();
    TsChannel* get(TsPid pid);
    TsChannel* set(TsPid pid);

private:
    std::map<TsPid, TsChannel> _pids;
};

// The packet in ts stream.
// 2.4.3.2 Transport Stream packet layer, hls-mpeg-ts-iso13818-1.pdf, page 36
// Transport Stream packets shall be 188 bytes long.
class TsPacket {    
public:
    TsPacket(TsChannelGroup* c/*owned by TsWriter*/);
    ~TsPacket();

    void CreateAsPAT(int16_t pmt_number, TsPid pmt_pid);
    int CreateAsPMT(int16_t pmt_number, TsPid pmt_pid,
                    TsPid vpid, TsStream vs,
                    TsPid apid, TsStream as);
    void CreateAsPESFirst(TsPid pid, TsPESStreamId sid,
                        uint8_t continuity_counter, bool discontinuity,
                        int64_t pcr, int64_t dts, int64_t pts, int size);
    void CreateAsPESContinue(TsPid pid, uint8_t continuity_counter);
    
    // Returns the encoded size.
    size_t ByteSize() const;
    
    // Encode this TsPacket into `data' which shall be as long as ByteSize().
    // Returns 0 on success, -1 otherwise.
    int Encode(void* data) const;

    // Make ByteSize() bigger for `num_stuffings' bytes.
    void AddPadding(size_t num_stuffings);
    
    TsChannelGroup* channel_group() const { return _tschan_group; }

    bool payload_unit_start_indicator() const
    { return _payload_unit_start_indicator; }
    
    TsPid pid() const { return _pid; }

    bool has_adaptation_field() const { return _adaptation_field; }
    const TsAdaptationField& adaptation_field() const { return *_adaptation_field; }
    TsAdaptationField* mutable_adaptation_field()  {
        if (_adaptation_field) {
            return _adaptation_field;
        }
        return CreateAdaptationField();
    }

    bool has_payload() const {  return _payload; }
    const TsPayload& payload() const { return *_payload; }

private:
    void Reset();
    TsAdaptationField* CreateAdaptationField();

    // Set to true after any CreateAsXXX was called. Set to true in Reset
    bool _modified;
    
    // When set to '1' it indicates that at least 1 uncorrectable bit error
    // exists in the associated Transport Stream packet. This bit may be set
    // to '1' by entities external to the transport layer. When set to '1'
    // this bit shall not be reset to '0' unless the bit value(s) in error
    // have been corrected.
    int8_t _transport_error_indicator; // 1 bit

    // A flag which has normative meaning for Transport Stream packets that
    // carry PES packets (refer to 2.4.3.6) or PSI data (refer to 2.4.4).
    // - When the payload of the Transport Stream packet contains PES packet
    // data, this flag has the following significance: a '1' indicates that
    // the payload of this Transport Stream packet will commence(start) with
    // the first byte of a PES packet and a '0' indicates no PES packet shall
    // start in this Transport Stream packet. If this flag is set to '1',
    // then one and only one PES packet starts in this Transport Stream
    // packet. This also applies to private streams of stream_type 6 (refer
    // to Table 2-29).
    // - When the payload of the Transport Stream packet contains PSI data,
    // this flag has the following significance: if the Transport Stream
    // packet carries the first byte of a PSI section, this flag shall be '1',
    // indicating that the first byte of the payload of this Transport Stream
    // packet carries the pointer_field. If the Transport Stream packet does
    // not carry the first byte of a PSI section, this flag shall be '0',
    // indicating that there is no pointer_field in the payload. Refer to
    // 2.4.4.1 and 2.4.4.2. This also applies to private streams of stream_type
    // 5 (refer to Table 2-29).
    // - For null packets this flag shall be set to '0'.
    // - The meaning of this bit for Transport Stream packets carrying only
    // private data is not defined in this Specification.
    int8_t _payload_unit_start_indicator; // 1 bit

    // When set to '1' it indicates that the associated packet is of greater
    // priority than other packets having the same PID which do not have the
    // bit set to '1'. The transport mechanism can use this to prioritize its
    // data within an elementary stream. Depending on the application the
    // transport_priority field may be coded regardless of the PID or within
    // one PID only. This field may be changed by channel specific encoders or
    // decoders.
    int8_t _transport_priority; // 1 bit

    // Indicate the type of the data stored in the packet payload.
    // 0x0000: reserved for the Program Association Table (see Table 2-25).
    // 0x0001: reserved for the Conditional Access Table (see Table 2-27).
    // 0x0002 - 0x000F: reserved.
    // 0x1FFF: reserved for null packets (see Table 2-3).
    TsPid _pid; // 13 bits

    // Indicate the scrambling mode of the Transport Stream packet payload.
    // The Transport Stream packet header, and the adaptation field when
    // present, shall not be scrambled. In the case of a null packet the
    // value of the transport_scrambling_control field shall be set to '00'
    // (see Table 2-4).
    TsScrambled _transport_scrambling_control; // 2 bits

    // Indicate whether this Transport Stream packet header is followed by an
    // adaptation field and/or payload (see Table 2-5).
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 decoders shall discard Transport
    // Stream packets with this flag set to '00'. In the case of a null packet
    // this flag shall be set to '01'.
    TsAdaptationFieldType _adaptation_field_control; // 2 bits

    // A field incrementing with each Transport Stream packet with the
    // same PID. The continuity_counter wraps around to 0 after its maximum
    // value. The continuity_counter shall not be incremented when the
    // adaptation_field_control of the packet equals '00'(reseverd) or '10'
    // (adaptation field only).
    // In Transport Streams, duplicate packets may be sent as two, and only
    // two, consecutive Transport Stream packets of the same PID. The
    // duplicate packets shall have the same continuity_counter value as the
    // original packet and the adaptation_field_control field shall be equal
    // to '01'(payload only) or '11'(both). In duplicate packets each byte
    // of the original packet shall be duplicated, with the exception that in
    // the program clock reference fields, if present, a valid value shall be
    // encoded.
    // The continuity_counter in a particular Transport Stream packet is
    // continuous when it differs by a positive value of one from the
    // continuity_counter value in the previous Transport Stream packet of the
    // same PID, or when either of the nonincrementing conditions
    // (adaptation_field_control set to '00' or '10', or duplicate packets as
    // described above) are met.
    // The continuity counter may be discontinuous when the
    // discontinuity_indicator is set to '1' (refer to 2.4.3.4). In the case
    // of a null packet the value of the continuity_counter is undefined.
    uint8_t _continuity_counter; // 4 bits

    // Optional.
    // TODO: Is it worthwhile to new?
    TsAdaptationField* _adaptation_field;
    TsPayload* _payload;
    
    TsChannelGroup* _tschan_group;
};

// The adaptation field of ts packet. 2.4.3.5 Semantic definition of fields in
// adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 39.
// Table 2-6 - Transport Stream adaptation field, hls-mpeg-ts-iso13818-1.pdf,
// page 40
class TsAdaptationField {
public:
    TsAdaptationField();
    ~TsAdaptationField();

    // Returns the encoded size.
    size_t ByteSize() const;

    // Encode into `data' according to `adaptation_field_control'.
    // Returns 0 on success, -1 otherwise.
    int Encode(void* data, TsAdaptationFieldType adaptation_field_control) const;
    
public:
    // The number of bytes in the adaptation_field immediately following the
    // adaptation_field_length. The value 0 is for inserting a single stuffing
    // byte in a Transport Stream packet.
    // When the adaptation_field_control value is '11', this field shall be in
    // the range 0 to 182.
    // When the adaptation_field_control value is '10', this field shall be 183.
    // For Transport Stream packets carrying PES packets, stuffing is needed
    // when there is insufficient PES packet data to completely fill the
    // Transport Stream packet payload bytes. Stuffing is accomplished by
    // defining an adaptation field longer than the sum of the lengths of the
    // data elements in it, so that the payload bytes remaining after the
    // adaptation field exactly accommodates the available PES packet data.
    // The extra space in the adaptation field is filled with stuffing bytes.
    // This is the only method of stuffing allowed for Transport Stream packets
    // carrying PES packets. For Transport Stream packets carrying PSI, an
    // alternative stuffing method is described in 2.4.4.
    uint8_t adaptation_field_length() const { return ByteSize() - 1; }
    
    // When set to '1' indicates that the discontinuity state is true for the
    // current Transport Stream packet.
    // When set to '0' or is not present, the discontinuity state is false.
    // The discontinuity indicator is used to indicate two types of
    // discontinuities, system time-base discontinuities and continuity_counter
    // discontinuities.
    // A system time-base discontinuity is indicated by the use of the
    // discontinuity_indicator in Transport Stream packets of a PID designated
    // as a PCR_PID (refer to 2.4.4.9). When the discontinuity state is true
    // for a Transport Stream packet of a PID designated as a PCR_PID, the next
    // PCR in a Transport Stream packet with that same PID represents a sample
    // of a new system time clock for the associated program. The system
    // time-base discontinuity point is defined to be the instant in time when
    // the first byte of a packet containing a PCR of a new system time-base
    // arrives at the input of the T-STD.
    // The discontinuity_indicator shall be set to '1' in the packet in which
    // the system time-base discontinuity occurs. The discontinuity_indicator
    // bit may also be set to '1' in Transport Stream packets of the same
    // PCR_PID prior to the packet which contains the new system time-base PCR.
    // In this case, once the discontinuity_indicator has been set to '1', it
    // shall continue to be set to '1' in all Transport Stream packets of the
    // same PCR_PID up to and including the Transport Stream packet which
    // contains the first PCR of the new system time-base. After the occurrence
    // of a system time-base discontinuity, no fewer than two PCRs for the new
    // system time-base shall be received before another system time-base
    // discontinuity can occur. Further, except when trick mode status is true,
    // data from no more than two system time-bases shall be present in the
    // set of T-STD buffers for one program at any time.
    // Prior to the occurrence of a system time-base discontinuity, the first
    // byte of a Transport Stream packet which contains a PTS or DTS which
    // refers to the new system time-base shall not arrive at the input of
    // the T-STD. After the occurrence of a system time-base discontinuity,
    // the first byte of a Transport Stream packet which contains a PTS or DTS
    // which refers to the previous system time-base shall not arrive at the
    // input of the T-STD.
    // A continuity_counter discontinuity is indicated by the use of the
    // discontinuity_indicator in any Transport Stream packet. When the
    // discontinuity state is true in any Transport Stream packet of a PID
    // not designated as a PCR_PID, the continuity_counter in that packet may
    // be discontinuous with respect to the previous Transport Stream packet
    // of the same PID. When the discontinuity state is true in a Transport
    // Stream packet of a PID that is designated as a PCR_PID, the
    // continuity_counter may only be discontinuous in the packet in which
    // a system time-base discontinuity occurs. A continuity counter
    // discontinuity point occurs when the discontinuity state is true in a
    // Transport Stream packet and the continuity_counter in the same packet
    // is discontinuous with respect to the previous Transport Stream packet
    // of the same PID. A continuity counter discontinuity point shall occur
    // at most one time from the initiation of the discontinuity state until
    // the conclusion of the discontinuity state. Furthermore, for all PIDs
    // that are not designated as PCR_PIDs, when the discontinuity_indicator
    // is set to '1' in a packet of a specific PID, the discontinuity_indicator
    // may be set to '1' in the next Transport Stream packet of that same PID,
    // but shall not be set to '1' in three consecutive Transport Stream packet
    // of that same PID.
    // For the purpose of this clause, an elementary stream access point is
    // defined as follows:
    //       Video - The first byte of a video sequence header.
    //       Audio - The first byte of an audio frame.
    // After a continuity counter discontinuity in a Transport packet which is
    // designated as containing elementary stream data, the first byte of
    // elementary stream data in a Transport Stream packet of the same PID
    // shall be the first byte of an elementary stream access point or in the
    // case of video, the first byte of an elementary stream access point or
    // a sequence_end_code followed by an access point. Each Transport Stream
    // packet which contains elementary stream data with a PID not designated
    // as a PCR_PID, and in which a continuity counter discontinuity point
    // occurs, and in which a PTS or DTS occurs, shall arrive at the input of
    // the T-STD after the system time-base discontinuity for the associated
    // program occurs. In the case where the discontinuity state is true, if
    // two consecutive Transport Stream packets of the same PID occur which
    // have the same continuity_counter value and have adaptation_field_control
    // values set to '01' or '11', the second packet may be discarded. A
    // Transport Stream shall not be constructed in such a way that discarding
    // such a packet will cause the loss of PES packet payload data or PSI data.
    // After the occurrence of a discontinuity_indicator set to '1' in a
    // Transport Stream packet which contains PSI information, a single
    // discontinuity in the version_number of PSI sections may occur. At the
    // occurrence of such a discontinuity, a version of the
    // TS_program_map_sections of the appropriate program shall be sent with
    // section_length == 13 and the current_next_indicator == 1, such that
    // there are no program_descriptors and no elementary streams described.
    // This shall then be followed by a version of the TS_program_map_section
    // for each affected program with the version_number incremented by one
    // and the current_next_indicator == 1, containing a complete program
    // definition. This indicates a version change in PSI data.
    int8_t discontinuity_indicator; // 1 bit
    
    // Indicate that the current Transport Stream packet, and possibly
    // subsequent Transport Stream packets with the same PID, contain some
    // information to aid random access at this point.
    // Specifically, when the bit is set to '1', the next PES packet to start
    // in the payload of Transport Stream packets with the current PID shall
    // contain the first byte of a video sequence header if the PES stream
    // type (refer to Table 2-29) is 1 or 2, or shall contain the first byte
    // of an audio frame if the PES stream type is 3 or 4. In addition, in
    // the case of video, a presentation timestamp shall be present in the
    // PES packet containing the first picture following the sequence header.
    // In the case of audio, the presentation timestamp shall be present in
    // the PES packet containing the first byte of the audio frame. In the
    // PCR_PID the random_access_indicator may only be set to '1' in Transport
    // Stream packet containing the PCR fields.
    int8_t random_access_indicator; // 1 bit
    
    // Indicate among packets with the same PID, the priority of the elementary
    // stream data carried within the payload of this Transport Stream packet.
    // A '1' indicates that the payload has a higher priority than the payloads
    // of other Transport Stream packets. In the case of video, this field may
    // be set to '1' only if the payload contains one or more bytes from an
    // intra-coded slice.
    // A value of '0' indicates that the payload has the same priority as all
    // other packets which do not have this bit set to '1'.
    int8_t elementary_stream_priority_indicator; // 1 bit
    
    // '1' indicates that the adaptation_field contains a PCR field coded in
    // two parts.
    // '0' indicates that the adaptation field does not contain any PCR field.
    int8_t PCR_flag; // 1 bit
    
    // '1' indicates that the adaptation_field contains an OPCR field coded
    // in two parts.
    // '0' indicates that the adaptation field does not contain any OPCR field.
    int8_t OPCR_flag; // 1 bit
    
    // '1' indicates that a splice_countdown field shall be present in the
    // associated adaptation field, specifying the occurrence of a splicing
    // point.
    // '0' indicates that a splice_countdown field is not present in the
    // adaptation field.
    int8_t splicing_point_flag; // 1 bit
    
    // '1' indicates that the adaptation field contains one or more
    // private_data bytes.
    // '0' indicates the adaptation field does not contain any private_data
    // bytes.
    int8_t transport_private_data_flag; // 1 bit
    
    // '1' indicates the presence of an adaptation field extension.
    // '0' indicates that an adaptation field extension is not present in
    // the adaptation field.
    int8_t adaptation_field_extension_flag; // 1 bit
    
    // The program_clock_reference(PCR) is a 42-bit field coded in two parts.
    // The first part, program_clock_reference_base, is a 33-bit field whose
    // value is given by PCR_base(i), as given in equation 2-2. The second part,
    // program_clock_reference_extension, is a 9-bit field whose value is
    // given by PCR_ext(i), as given in equation 2-3. The PCR indicates the
    // intended time of arrival of the byte containing the last bit of the
    // program_clock_reference_base at the input of the system target decoder.
    int64_t program_clock_reference_base; // 33 bits
    static const int8_t const1_value0 = 0x3F; // 6 bits reserved
    int16_t program_clock_reference_extension; // 9 bits
    
    // The optional original program reference (OPCR) is a 42-bit field coded
    // in two parts. These two parts, the base and the extension, are coded
    // identically to the two corresponding parts of the PCR field. The presence
    // of the OPCR is indicated by the OPCR_flag. The OPCR field shall be coded
    // only in Transport Stream packets in which the PCR field is present.
    // OPCRs are permitted in both single program and multiple program Transport
    // Streams. OPCR assists in the reconstruction of a single program Transport
    // Stream from another Transport Stream. When reconstructing the original
    // single program Transport Stream, the OPCR may be copied to the PCR field.
    // The resulting PCR value is valid only if the original single program
    // Transport Stream is reconstructed exactly in its entirety. This would
    // include at least any PSI and private data packets which were present
    // in the original Transport Stream and would possibly require other
    // private arrangements. It also means that the OPCR must be an identical
    // copy of its associated PCR in the original single program Transport
    // Stream.
    int64_t original_program_clock_reference_base; // 33 bits
    static const int8_t const1_value2 = 0x3F; // 6 bits reserved
    int16_t original_program_clock_reference_extension; // 9 bits
    
    // A positive value specifies the remaining number of Transport Stream
    // packets, of the same PID, following the associated Transport Stream
    // packet until a splicing point is reached. Duplicate Transport Stream
    // packets and Transport Stream packets which only contain adaptation
    // fields are excluded. The splicing point is located immediately after
    // the last byte of the Transport Stream packet in which the associated
    // splice_countdown field reaches zero. In the Transport Stream packet
    // where the splice_countdown reaches zero, the last data byte of the
    // Transport Stream packet payload shall be the last byte of a coded
    // audio frame or a coded picture. In the case of video, the corresponding
    // access unit may or may not be terminated by a sequence_end_code.
    // Transport Stream packets with the same PID, which follow, may contain
    // data from a different elementary stream of the same type.
    // The payload of the next Transport Stream packet of the same PID
    // (duplicate packets and packets without payload being excluded) shall
    // commence with the first byte of a PES packet. In the case of audio, the
    // PES packet payload shall commence with an access point. In the case of
    // video, the PES packet payload shall commence with an access point, or
    // with a sequence_end_code, followed by an access point. Thus, the
    // previous coded audio frame or coded picture aligns with the packet
    // boundary, or is padded to make this so. Subsequent to the splicing
    // point, the countdown field may also be present. When the splice_countdown
    // is a negative number whose value is minus n(-n), it indicates that
    // the associated Transport Stream packet is the n-th packet following
    // the splicing point (duplicate packets and packets without payload
    // being excluded).
    // For the purposes of this subclause, an access point is defined as follows:
    //       Video - The first byte of a video_sequence_header.
    //       Audio - The first byte of an audio frame.
    int8_t splice_countdown; // 8 bits
    
    // The transport_private_data_length is an 8-bit field specifying the
    // number of private_data bytes immediately following the transport
    // private_data_length field. The number of private_data bytes shall
    // not be such that private data extends beyond the adaptation field.
    uint8_t transport_private_data_length; // 8 bits
    char* transport_private_data; // [transport_private_data_length] bytes
    
    // The adaptation_field_extension_length is an 8-bit field. It indicates
    // the number of bytes of the extended adaptation field data immediately
    // following this field, including reserved bytes if present.
    uint8_t adaptation_field_extension_length; // 8 bits
    
    // '1' indicates the presence of the ltw_offset field.
    int8_t ltw_flag; // 1 bit
    
    // '1' indicates the presence of the piecewise_rate field.
    int8_t piecewise_rate_flag; // 1 bit

    // When set to '1' indicates that the splice_type and DTS_next_AU fields
    // are present. A value of '0' indicates that neither splice_type nor
    // DTS_next_AU fields are present. This field shall not be set to '1' in
    // Transport Stream packets in which the splicing_point_flag is not set
    // to '1'. Once it is set to '1' in a Transport Stream packet in which
    // the splice_countdown is positive, it shall be set to '1' in all the
    // subsequent Transport Stream packets of the same PID that have the
    // splicing_point_flag set to '1', until the packet in which the
    // splice_countdown reaches zero (including this packet). When this flag
    // is set, if the elementary stream carried in this PID is an audio stream,
    // the splice_type field shall be set to '0000'. If the elementary stream
    // carried in this PID is a video stream, it shall fulfil the constraints
    // indicated by the splice_type value.
    int8_t seamless_splice_flag; // 1 bit
    static const int8_t const1_value1 = 0x1F; // reserved 5bits

    // '1' indicates that the value of the ltw_offset shall be valid.
    // '0' indicates that the value in the ltw_offset field is undefined.
    int8_t ltw_valid_flag; // 1 bit
    
    // A 15-bit field, the value of which is defined only if the ltw_valid
    // flag has a value of '1'. When defined, the legal time window offset
    // is in units of (300/fs) seconds, where fs is the system clock frequency
    // of the program that this PID belongs to, and fulfils:
    //       offset = t1(i) - t(i)
    //       ltw_offset = offset//1
    // where i is the index of the first byte of this Transport Stream packet,
    // offset is the value encoded in this field, t(i) is the arrival time of
    // byte i in the T-STD, and t1(i) is the upper bound in time of a time
    // interval called the Legal Time Window which is associated with this
    // Transport Stream packet.
    int16_t ltw_offset; // 15 bits
    
    // The meaning of this 22-bit field is only defined when both the ltw_flag
    // and the ltw_valid_flag are set to '1'. When defined, it is a positive
    // integer specifying a hypothetical bitrate R which is used to define
    // the end times of the Legal Time Windows of Transport Stream packets
    // of the same PID that follow this packet but do not include the
    // legal_time_window_offset field.
    int32_t piecewise_rate; // 22 bits
    
    // This is a 4-bit field. From the first occurrence of this field onwards,
    // it shall have the same value in all the subsequent Transport Stream
    // packets of the same PID in which it is present, until the packet in
    // which the splice_countdown reaches zero (including this packet). If
    // the elementary stream carried in that PID is an audio stream, this
    // field shall have the value '0000'. If the elementary stream carried
    // in that PID is a video stream, this field indicates the conditions
    // that shall be respected by this elementary stream for splicing purposes.
    // These conditions are defined as a function of profile, level and
    // splice_type in Table 2-7 through Table 2-16.
    int8_t splice_type; // 4 bits
    
    // This is a 33-bit field, coded in three parts. In the case of continuous
    // and periodic decoding through this splicing point it indicates the
    // decoding time of the first access unit following the splicing point.
    // This decoding time is expressed in the time base which is valid in the
    // Transport Stream packet in which the splice_countdown reaches zero.
    // From the first occurrence of this field onwards, it shall have the same
    // value in all the subsequent Transport Stream packets of the same PID
    // in which it is present, until the packet in which the splice_countdown
    // reaches zero (including this packet).
    int8_t DTS_next_AU0;  // 3 bits
    int8_t marker_bit0;   // 1 bit
    int16_t DTS_next_AU1; // 15 bits
    int8_t marker_bit1;   // 1 bit
    int16_t DTS_next_AU2; // 15 bits
    int8_t marker_bit2;   // 1 bit
    
    // This is a fixed 8-bit value equal to '1111 1111' that can be inserted
    // by the encoder. It is discarded by the decoder.
    int nb_af_ext_reserved;
    
    // This is a fixed 8-bit value equal to '1111 1111' that can be inserted
    // by the encoder. It is discarded by the decoder.
    int nb_af_reserved;
};

// the payload of ts packet, can be PES or PSI payload.
class TsPayload {
public:
    explicit TsPayload(const TsPacket* p);
    virtual ~TsPayload();
    virtual size_t ByteSize() const = 0;
    virtual int Encode(void* data) const = 0;
    const TsPacket* packet() const { return _packet; }
private:
    const TsPacket* _packet;
};

// the PES payload of ts packet.
// 2.4.3.6 PES packet, hls-mpeg-ts-iso13818-1.pdf, page 49
class TsPayloadPES : public TsPayload {
public:
    explicit TsPayloadPES(const TsPacket* p);
    virtual ~TsPayloadPES();
    virtual size_t ByteSize() const;
    virtual int Encode(void* data) const;
private:
    void encode_33bits_dts_pts(char** data, uint8_t fb, int64_t v) const;
    // Specify the total number of bytes occupied by the optional fields and
    // any stuffing bytes contained in this PES packet header. The presence
    // of optional fields is indicated in the byte that precedes the
    // PES_header_data_length field.
    mutable int16_t _PES_header_data_length; // 8 bits
    
public:
    // In Program Streams, the stream_id specifies the type and number of the
    // elementary stream as defined by the stream_id Table 2-18. In Transport
    // Streams, the stream_id may be set to any valid value which correctly
    // describes the elementary stream type as defined in Table 2-18. In
    // Transport Streams, the elementary stream type is specified in the
    // Program Specific Information as specified in 2.4.4.
    TsPESStreamId stream_id; // 8 bits
    
    // Specify the number of bytes in the PES packet following the last byte
    // of the field. A value of 0 indicates that the PES packet length is
    // neither specified nor bounded and is allowed only in PES packets whose
    // payload consists of bytes from a video elementary stream contained in
    // Transport Stream packets.
    uint16_t PES_packet_length; // 16 bits

    static const int8_t const2bits = 0x02; // 2 bits const '10'

    // Indicate the scrambling mode of the PES packet payload. When scrambling
    // is performed at the PES level, the PES packet header, including the
    // optional fields when present, shall not be scrambled (see Table 2-19).
    int8_t PES_scrambling_control; // 2 bits
    
    // This is a 1-bit field indicating the priority of the payload in this PES
    // packet. A '1' indicates a higher priority of the payload of the PES
    // packet payload than a PES packet payload with this field set to '0'.
    // A multiplexor can use the PES_priority bit to prioritize its data within
    // an elementary stream. This field shall not be changed by the transport
    // mechanism.
    int8_t PES_priority; // 1 bit
    
    // When set to a value of '1' it indicates that the PES packet header is
    // immediately followed by the video start code or audio syncword indicated
    // in the data_stream_alignment_descriptor in 2.6.10 if this descriptor is
    // present. If set to a value of '1' and the descriptor is not present,
    // alignment as indicated in alignment_type '01' in Table 2-47 and Table
    // 2-48 is required. When set to a value of '0' it is not defined whether
    // any such alignment occurs or not.
    int8_t data_alignment_indicator; // 1 bit
    
    // When set to '1' it indicates that the material of the associated PES
    // packet payload is protected by copyright. When set to '0' it is not
    // defined whether the material is protected by copyright. A copyright
    // descriptor described in 2.6.24 is associated with the elementary
    // stream which contains this PES packet and the copyright flag is set
    // to '1' if the descriptor applies to the material contained in this
    // PES packet.
    int8_t copyright; // 1 bit

    // When set to '1' the contents of the associated PES packet payload is
    // an original. When set to '0' it indicates that the contents of the
    // associated PES packet payload is a copy.
    int8_t original_or_copy; // 1 bit

    // When the PTS_DTS_flags field is set to '10', the PTS fields shall be
    // present in the PES packet header. When the PTS_DTS_flags field is set
    // to '11', both the PTS fields and DTS fields shall be present in the
    // PES packet header. When the PTS_DTS_flags field is set to '00' no PTS
    // or DTS fields shall be present in the PES packet header. The value
    // '01' is forbidden.
    int8_t PTS_DTS_flags; // 2 bits
    
    // When set to '1' indicates that ESCR base and extension fields are
    // present in the PES packet header. When set to '0' it indicates that
    // no ESCR fields are present.
    int8_t ESCR_flag; // 1 bit
    
    // When set to '1' indicates that the ES_rate field is present in the PES
    // packet header. When set to '0' it indicates that no ES_rate field is
    // present.
    int8_t ES_rate_flag; // 1 bit
    
    // When set to '1' it indicates the presence of an 8-bit trick mode field.
    // When set to '0' it indicates that this field is not present.
    int8_t DSM_trick_mode_flag; // 1 bit
    
    // when set to '1' indicates the presence of the additional_copy_info field.
    // When set to '0' it indicates that this field is not present.
    int8_t additional_copy_info_flag; // 1 bit
    
    // when set to '1' indicates that a CRC field is present in the PES packet.
    // When set to '0' it indicates that this field is not present.
    int8_t PES_CRC_flag; // 1 bit
    
    // when set to '1' indicates that an extension field exists in this PES
    // packet header.
    // When set to '0' it indicates that this field is not present.
    int8_t PES_extension_flag; // 1 bit

    // Presentation times shall be related to decoding times as follows:
    // The PTS is a 33-bit number coded in three separate fields. It indicates
    // the time of presentation, tp n (k), in the system target decoder of a
    // presentation unit k of elementary stream n. The value of PTS is
    // specified in units of the period of the system clock frequency divided
    // by 300 (yielding 90 kHz). The presentation time is derived from the PTS
    // according to equation 2-11 below. Refer to 2.7.4 for constraints on the
    // frequency of coding presentation timestamps.
    //   ===========1B
    //   4bits const
    //   3bits PTS [32..30]
    //   1bit const '1'
    //   ===========2B
    //   15bits PTS [29..15]
    //   1bit const '1'
    //   ===========2B
    //   15bits PTS [14..0]
    //   1bit const '1'
    int64_t pts; // 33 bits

    // The DTS is a 33-bit number coded in three separate fields. It indicates
    // the decoding time, td n (j), in the system target decoder of an access
    // unit j of elementary stream n. The value of DTS is specified in units
    // of the period of the system clock frequency divided by 300
    // (yielding 90 kHz).
    //   ===========1B
    //   4bits const
    //   3bits DTS [32..30]
    //   1bit const '1'
    //   ===========2B
    //   15bits DTS [29..15]
    //   1bit const '1'
    //   ===========2B
    //   15bits DTS [14..0]
    //   1bit const '1'
    int64_t dts; // 33 bits

    // The elementary stream clock reference is a 42-bit field coded in two
    // parts. The first part, ESCR_base, is a 33-bit field whose value is given
    // by ESCR_base(i), as given in equation 2-14. The second part, ESCR_ext,
    // is a 9-bit field whose value is given by ESCR_ext(i), as given in
    // equation 2-15. The ESCR field indicates the intended time of arrival
    // of the byte containing the last bit of the ESCR_base at the input of
    // the PES-STD for PES streams (refer to 2.5.2.4).
    //   2bits reserved
    //   3bits ESCR_base[32..30]
    //   1bit const '1'
    //   15bits ESCR_base[29..15]
    //   1bit const '1'
    //   15bits ESCR_base[14..0]
    //   1bit const '1'
    //   9bits ESCR_extension
    //   1bit const '1'
    int64_t ESCR_base; // 33 bits
    int16_t ESCR_extension; // 9 bits

    // The ES_rate field is a 22-bit unsigned integer specifying the rate at
    // which the system target decoder receives bytes of the PES packet in
    // the case of a PES stream. The ES_rate is valid in the PES packet in
    // which it is included and in subsequent PES packets of the same PES
    // stream until a new ES_rate field is encountered. The value of the
    // ES_rate is measured in units of 50 bytes/second. The value 0 is
    // forbidden. The value of the ES_rate is used to define the time of
    // arrival of bytes at the input of a P-STD for PES streams defined in
    // 2.5.2.4. The value encoded in the ES_rate field may vary from
    // PES_packet to PES_packet.
    //   1bit const '1'
    //   22bits ES_rate
    //   1bit const '1'
    int32_t ES_rate; // 22 bits

    // Indicates which trick mode is applied to the associated video stream.
    // In cases of other types of elementary streams, the meanings of this
    // field and those defined by the following five bits are undefined. For
    // the definition of trick_mode status, refer to the trick mode section
    // of 2.4.2.3.
    int8_t trick_mode_control; // 3 bits
    int8_t trick_mode_value; // 5 bits

    // 1bit const '1'
    
    // This 7-bit field contains private data relating to copyright information.
    int8_t additional_copy_info; //7bits

    // Contain the CRC value that yields a zero output of the 16 registers
    // in the decoder similar to the one defined in Annex A
    int16_t previous_PES_packet_CRC; // 16 bits

    // when set to '1' indicates that the PES packet header contains private
    // data.
    // When set to a value of '0' it indicates that private data is not present
    // in the PES header.
    int8_t PES_private_data_flag; // 1 bit
    
    // when set to '1' indicates that an ISO/IEC 11172-1 pack header or a
    // Program Stream pack header is stored in this PES packet header. If this
    // field is in a PES packet that is contained in a Program Stream, then
    // this field shall be set to '0'. In a Transport Stream, when set to the
    // value '0' it indicates that no pack header is present in the PES header.
    int8_t pack_header_field_flag; // 1 bit
    
    // When set to '1' indicates that the program_packet_sequence_counter,
    // MPEG1_MPEG2_identifier, and original_stuff_length fields are present
    // in this PES packet. When set to a value of '0' it indicates that these
    // fields are not present in the PES header.
    int8_t program_packet_sequence_counter_flag; // 1 bit
    
    // When set to '1' indicates that the P-STD_buffer_scale and
    // P-STD_buffer_size are present in the PES packet header. When set to a
    // value of '0' it indicates that these fields are not present in the PES
    // header.
    int8_t P_STD_buffer_flag; // 1 bit

    static const int8_t const1_value0 = 0x07; // 3 bits reserved
    
    // When set to '1' indicates the presence of the PES_extension_field_length
    // field and associated fields. When set to a value of '0' this indicates
    // that the PES_extension_field_length field and any associated fields are
    // not present.
    int8_t PES_extension_flag_2; // 1 bit

    // Contain private data. This data, combined with the fields before and
    // after, shall not emulate the packet_start_code_prefix (0x000001).
    char* PES_private_data; // 128 bits

    // Indicates the length in bytes, of the pack_header_field().
    uint8_t pack_field_length; // 8 bits
    char* pack_field; //[pack_field_length] bytes

    // 1bit const '1'
    
    // The program_packet_sequence_counter field is a 7-bit field. It is an
    // optional counter that increments with each successive PES packet from a
    // Program Stream or from an ISO/IEC 11172-1 Stream or the PES packets
    // associated with a single program definition in a Transport Stream,
    // providing functionality similar to a continuity counter (refer to
    // 2.4.3.2). This allows an application to retrieve the original PES
    // packet sequence of a Program Stream or the original packet sequence of
    // the original ISO/IEC 11172-1 stream. The counter will wrap around to 0
    // after its maximum value. Repetition of PES packets shall not occur.
    // Consequently, no two consecutive PES packets in the program multiplex
    // shall have identical program_packet_sequence_counter values.
    int8_t program_packet_sequence_counter; // 7 bits

    // 1bit const '1'

    // When set to '1' indicates that this PES packet carries information from
    // an ISO/IEC 11172-1 stream. When set to '0' it indicates that this PES
    // packet carries information from a Program Stream.
    int8_t MPEG1_MPEG2_identifier; // 1 bit
    
    // Specify the number of stuffing bytes used in the original ITU-T
    // Rec. H.222.0 | ISO/IEC 13818-1 PES packet header or in the original
    // ISO/IEC 11172-1 packet header.
    int8_t original_stuff_length; // 6 bits

    // 2bits const '01'
    
    // The P-STD_buffer_scale is a 1-bit field, the meaning of which is only
    // defined if this PES packet is contained in a Program Stream. It
    // indicates the scaling factor used to interpret the subsequent
    // P-STD_buffer_size field. If the preceding stream_id indicates an audio
    // stream, P-STD_buffer_scale shall have the value '0'. If the preceding
    // stream_id indicates a video stream, P-STD_buffer_scale shall have the
    // value '1'. For all other stream types, the value may be either '1'
    // or '0'.
    int8_t P_STD_buffer_scale; // 1 bit
    
    // The P-STD_buffer_size is a 13-bit unsigned integer, the meaning of
    // which is only defined if this PES packet is contained in a Program
    // Stream. It defines the size of the input buffer, BS n , in the P-STD.
    // If P-STD_buffer_scale has the value '0', then the P-STD_buffer_size
    // measures the buffer size in units of 128 bytes. If P-STD_buffer_scale
    // has the value '1', then the P-STD_buffer_size measures the buffer size
    // in units of 1024 bytes.
    int16_t P_STD_buffer_size; // 13 bits

    // 1bit const '1'

    // Specify the length in bytes, of the data following this field in the
    // PES extension field up to and including any reserved bytes.
    uint8_t PES_extension_field_length; // 7 bits
    char* PES_extension_field; //[PES_extension_field_length] bytes

    // A fixed 8-bit value equal to '1111 1111' that can be inserted by the
    // encoder, for example to meet the requirements of the channel.
    // It is discarded by the decoder. No more than 32 stuffing bytes shall
    // be present in one PES packet header.
    int nb_stuffings;

    // PES_packet_data_bytes shall be contiguous bytes of data from the
    // elementary stream indicated by the packet's stream_id or PID. When the
    // elementary stream data conforms to ITU-T Rec. H.262 | ISO/IEC 13818-2
    // or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to
    // the bytes of this Recommendation | International Standard. The
    // byte-order of the elementary stream shall be preserved. The number
    // of PES_packet_data_bytes, N, is specified by the PES_packet_length
    // field. N shall be equal to the value indicated in the PES_packet_length
    // minus the number of bytes between the last byte of the PES_packet_length
    // field and the first PES_packet_data_byte.
    // In the case of a private_stream_1, private_stream_2, ECM_stream, or
    // EMM_stream, the contents of the PES_packet_data_byte field are user
    // definable and will not be specified by ITU-T | ISO/IEC in the future.
    int nb_bytes;

    // A fixed 8-bit value equal to '1111 1111'. It is discarded by the decoder.
    int nb_paddings;
};

// the PSI payload of ts packet.
// 2.4.4 Program specific information, hls-mpeg-ts-iso13818-1.pdf, page 59
class TsPayloadPSI : public TsPayload {
public:
    explicit TsPayloadPSI(const TsPacket* p);
    virtual ~TsPayloadPSI();
    virtual size_t ByteSize() const;
    virtual int Encode(void* data) const;
protected:
    virtual size_t PsiByteSize() const = 0;
    virtual int PsiEncode(void* data) const = 0;
private:
    // This is a 12-bit field, the first two bits of which shall be '00'.
    // The remaining 10 bits specify the number of bytes of the section,
    // starting immediately following the section_length field, and including
    // the CRC. The value in this field shall not exceed 1021 (0x3FD).
    mutable int16_t _section_length; //12bits

public:
    // This is an 8-bit field whose value shall be the number of bytes,
    // immediately following the pointer_field until the first byte of the
    // first section that is present in the payload of the Transport Stream
    // packet (so a value of 0x00 in the pointer_field indicates that the
    // section starts immediately after the pointer_field). When at least
    // one section begins in a given Transport Stream packet, then the
    // payload_unit_start_indicator (refer to 2.4.3.2) shall be set to 1 and
    // the first byte of the payload of that Transport Stream packet shall
    // contain the pointer. When no section begins in a given Transport Stream
    // packet, then the payload_unit_start_indicator shall be set to 0 and
    // no pointer shall be sent in the payload of that packet.
    int8_t pointer_field;
    
    // A 1-bit field which shall be set to '1'.
    int8_t section_syntax_indicator; //1bit

    // An 8-bit field which shall be set to 0x00 as shown in Table 2-26.
    TsPsiId table_id; //8bits
    
    static const int8_t const0_value = 0; //1bit, must be '0'
    static const int8_t const1_value = 3; //2bits, reverved value, must be '1'
};

// the program of PAT of PSI ts packet.
class TsPayloadPATProgram {
public:
    TsPayloadPATProgram(int16_t number, TsPid pid);
    ~TsPayloadPATProgram();
    size_t ByteSize() const { return 4; }
    int Encode(void* data) const;

public:
    // Specify the program to which the program_map_PID is applicable. When set
    // to 0x0000, then the following PID reference shall be the network PID.
    // For all other cases the value of this field is user defined. This field
    // shall not take any single value more than once within one version of
    // the Program Association Table.
    int16_t program_number; // 16bits
    
    static const int8_t const1_value = 7; //3 bits, reserved
    
    // network_PID - a 13-bit field, which is used only in conjunction with
    // the value of the program_number set to 0x0000, specifies the PID of
    // the Transport Stream packets which shall contain the Network
    // Information Table. The value of the network_PID field is defined by
    // the user, but shall only take values as specified in Table 2-3. The
    // presence of the network_PID is optional.
    TsPid pid; //13bits
};

// the PAT payload of PSI ts packet.
// 2.4.4.3 Program association Table, hls-mpeg-ts-iso13818-1.pdf, page 61
// The Program Association Table provides the correspondence between a
// program_number and the PID value of the Transport Stream packets which
// carry the program definition. The program_number is the numeric label
// associated with a program.
class TsPayloadPAT : public TsPayloadPSI {
public:
    explicit TsPayloadPAT(const TsPacket* p);
    virtual ~TsPayloadPAT();
protected:
    virtual size_t PsiByteSize() const;
    virtual int PsiEncode(void* data) const;

public:
    // A 16-bit field which serves as a label to identify this Transport
    // Stream from any other multiplex within a network. Its value is
    // defined by the user.
    uint16_t transport_stream_id; //16bits
    
    static const int8_t const3_value = 3; //2bits, reserved
    
    // This 5-bit field is the version number of the whole Program Association
    // Table. The version number shall be incremented by 1 modulo 32 whenever
    // the definition of the Program Association Table changes. When the
    // current_next_indicator is set to '1', then the version_number shall be
    // that of the currently applicable Program Association Table. When the
    // current_next_indicator is set to '0', then the version_number shall
    // be that of the next applicable Program Association Table.
    int8_t version_number; //5bits
    
    // A 1-bit indicator, which when set to '1' indicates that the Program
    // Association Table sent is currently applicable. When the bit is set
    // to '0', it indicates that the table sent is not yet applicable and
    // shall be the next table to become valid.
    int8_t current_next_indicator; //1bit
    
    // This 8-bit field gives the number of this section. The section_number
    // of the first section in the Program Association Table shall be 0x00.
    // It shall be incremented by 1 with each additional section in the
    // Program Association Table.
    uint8_t section_number; //8bits
    
    // This 8-bit field specifies the number of the last section (that is,
    // the section with the highest section_number) of the complete Program
    // Association Table.
    uint8_t last_section_number; //8bits
    
    // multiple 4B program data.
    std::vector<TsPayloadPATProgram> programs;
};

// the esinfo for PMT program.
class TsPayloadPMTESInfo {
public:
    TsPayloadPMTESInfo(TsStream st, TsPid epid);
    ~TsPayloadPMTESInfo();
    size_t ByteSize() const;
    int Encode(void* data) const;

public:
    // An 8-bit field specifying the type of program element carried within
    // the packets with the PID whose value is specified by the elementary_PID.
    // The values of stream_type are specified in Table 2-29.
    TsStream stream_type; //8bits
    
    static const int8_t const1_value0 = 7; //3bits, reserved

    // Specify the PID of the Transport Stream packets which carry the
    // associated program element.
    TsPid elementary_PID; //13bits
    
    static const int8_t const1_value1 = 0xf; //4bits, reserved
    
    // the first two bits of which shall be '00'. The remaining 10 bits
    // specify the number of bytes of the descriptors of the associated
    // program element immediately following the ES_info_length field.
    int16_t ES_info_length; //12bits
    char* ES_info; //[ES_info_length] bytes.    
};

// the PMT payload of PSI ts packet.
// 2.4.4.8 Program Map Table, hls-mpeg-ts-iso13818-1.pdf, page 64
// The Program Map Table provides the mappings between program numbers and the
// program elements that comprise them. A single instance of such a mapping is
// referred to as a "program definition". The program map table is the complete
// collection of all program definitions for a Transport Stream. This table
// shall be transmitted in packets, the PID values of which are selected by
// the encoder. More than one PID value may be used, if desired. The table is
// contained in one or more sections with the following syntax. It may be
// segmented to occupy multiple sections. In each section, the section number
// field shall be set to zero. Sections are identified by the program_number
// field.
class TsPayloadPMT : public TsPayloadPSI {
public:
    explicit TsPayloadPMT(const TsPacket* p);
    virtual ~TsPayloadPMT();
protected:
    virtual size_t PsiByteSize() const;
    virtual int PsiEncode(void* data) const;

public:
    // Specify the program to which the program_map_PID is applicable. One
    // program definition shall be carried within only one
    // TS_program_map_section. This implies that a program definition is never
    // longer than 1016 (0x3F8). See Informative Annex C for ways to deal with
    // the cases when that length is not sufficient. The program_number may be
    // used as a designation for a broadcast channel, for example. By
    // describing the different program elements belonging to a program, data
    // from different sources (e.g. sequential events) can be concatenated
    // together to form a continuous set of streams using a program_number.
    // For examples of applications refer to Annex C.
    uint16_t program_number; //16bits
    
    static const int8_t const1_value0 = 3; //2bits, reserved
    
    // This 5-bit field is the version number of the TS_program_map_section.
    // The version number shall be incremented by 1 modulo 32 when a change
    // in the information carried within the section occurs. Version number
    // refers to the definition of a single program, and therefore to a
    // single section. When the current_next_indicator is set to '1', then
    // the version_number shall be that of the currently applicable
    // TS_program_map_section. When the current_next_indicator is set to '0',
    // then the version_number shall be that of the next applicable
    // TS_program_map_section.
    int8_t version_number; //5bits
    
    // A 1-bit field, which when set to '1' indicates that the
    // TS_program_map_section sent is currently applicable. When the bit is set
    // to '0', it indicates that the TS_program_map_section sent is not yet
    // applicable and shall be the next TS_program_map_section to become valid.
    int8_t current_next_indicator; //1bit
    
    // The value of this 8-bit field shall be 0x00.
    uint8_t section_number; //8bits
    
    // The value of this 8-bit field shall be 0x00.
    uint8_t last_section_number; //8bits
    
    static const int8_t const1_value1 = 7; //3bits, reserved
    
    // A 13-bit field indicating the PID of the Transport Stream packets which
    // shall contain the PCR fields valid for the program specified by
    // program_number. If no PCR is associated with a program definition for
    // private streams, then this field shall take the value of 0x1FFF. Refer
    // to the semantic definition of PCR in 2.4.3.5 and Table 2-3 for
    // restrictions on the choice of PCR_PID value.
    TsPid PCR_PID; //13bits
    
    static const int8_t const1_value2 = 0xf; //4bits, reserved
    
    // A 12-bit field, the first two bits of which shall be '00'. The
    // remaining 10 bits specify the number of bytes of the descriptors
    // immediately following the program_info_length field.
    uint16_t program_info_length; //12bits
    char* program_info_desc; //[program_info_length]bytes
    
    // array of TSPMTESInfo.
    std::vector<TsPayloadPMTESInfo*> infos;
};

// Convert Rtmp audio/video messages into ts packets.
class TsWriter {
public:
    explicit TsWriter(butil::IOBuf* outbuf);
    ~TsWriter();

    // Append a video/audio message into the output buffer.
    butil::Status Write(const RtmpVideoMessage&);
    butil::Status Write(const RtmpAudioMessage&);

    int64_t discontinuity_counter() const { return _discontinuity_counter; }
    void add_pat_pmt_on_next_write() { _encoded_pat_pmt = false; }
    
private:
    struct TsMessage;

    butil::Status Encode(TsMessage* msg, TsStream stream, TsPid pid);
    butil::Status EncodePATPMT(TsStream vs, TsPid vpid, TsStream as, TsPid apid);
    butil::Status EncodePES(TsMessage* msg, TsStream sid, TsPid pid, bool pure_audio);

    butil::IOBuf* _outbuf;
    AVCNaluFormat _nalu_format;
    bool _has_avc_seq_header;
    bool _has_aac_seq_header;
    bool _encoded_pat_pmt;
    AVCDecoderConfigurationRecord _avc_seq_header;
    AudioSpecificConfig _aac_seq_header;
    TsStream _last_video_stream;
    TsPid _last_video_pid;
    TsStream _last_audio_stream;
    TsPid _last_audio_pid;
    int64_t _discontinuity_counter;
    TsChannelGroup _tschan_group;
};

} // namespace brpc


#endif // BRPC_TS_H
