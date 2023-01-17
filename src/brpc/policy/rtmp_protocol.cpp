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


#include <openssl/hmac.h> // HMAC_CTX_init
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include "butil/scoped_lock.h"
#include "butil/fast_rand.h"
#include "butil/sys_byteorder.h"
#include "brpc/log.h"
#include "brpc/server.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/policy/dh.h"
#include "brpc/policy/rtmp_protocol.h"

// For printing logs with useful prefixes.
#define RTMP_LOG(level, socket, mh)                                     \
    LOG(level) << (socket)->remote_side() << '[' << (mh).stream_id << "] "
#define RTMP_ERROR(socket, mh) RTMP_LOG(ERROR, (socket), (mh))
#define RTMP_WARNING(socket, mh) RTMP_LOG(WARNING, (socket), (mh))

// Older openssl does not have EVP_sha256. To make the code always compile,
// we mark the symbol as weak. If the runtime does not have the function,
// handshaking will fallback to the simple one.
extern "C" {
const EVP_MD* __attribute__((weak)) EVP_sha256(void);
}


namespace brpc {

DECLARE_int64(socket_max_unwritten_bytes);
DECLARE_bool(use_normal_stack_for_keepwrite);

DEFINE_int32(rtmp_server_chunk_size, 60000,
             "Value of SetChunkSize sent to client before responding connect.");
DEFINE_int32(rtmp_server_window_ack_size, 2500000,
             "Value of WindowAckSize sent to client before responding connect.");

DEFINE_bool(rtmp_client_use_simple_handshake, true,
            "Use simple handshaking(the one in RTMP spec) to create client "
            "connections, false to use adobe proprietary handshake which "
            "consumes more CPU");
DEFINE_string(user_defined_data_message, "", 
            "extra name that user can specify in Data Message of RTMP, handled by OnMetaData");

namespace policy {

// Used in rtmp.cpp, don't be static
int WriteWithoutOvercrowded(Socket* s, SocketMessagePtr<>& msg) {
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    return s->Write(msg, &wopt);
}

const char* messagetype2str(RtmpMessageType t) {
    switch (t) {
    case RTMP_MESSAGE_SET_CHUNK_SIZE:        return "SetChunkSize";
    case RTMP_MESSAGE_ABORT:                 return "AbortMessage";
    case RTMP_MESSAGE_ACK:                   return "Ack";
    case RTMP_MESSAGE_USER_CONTROL:          return "UserControlMessage";
    case RTMP_MESSAGE_WINDOW_ACK_SIZE:       return "WindowAckSize";
    case RTMP_MESSAGE_SET_PEER_BANDWIDTH:    return "SetPeerBandwidth";
    case RTMP_MESSAGE_AUDIO:                 return "AudioMessage";
    case RTMP_MESSAGE_VIDEO:                 return "VideoMessage";
    case RTMP_MESSAGE_DATA_AMF3:             return "DataMessage_AMF3";
    case RTMP_MESSAGE_SHARED_OBJECT_AMF3:    return "SharedObjectMessage_AMF3";
    case RTMP_MESSAGE_COMMAND_AMF3:          return "CommandMessage_AMF3";
    case RTMP_MESSAGE_DATA_AMF0:             return "DataMessage_AMF0";
    case RTMP_MESSAGE_SHARED_OBJECT_AMF0:    return "SharedObjectMessage_AMF0";
    case RTMP_MESSAGE_COMMAND_AMF0:          return "CommandMessage_AMF0";
    case RTMP_MESSAGE_AGGREGATE:             return "AggregateMessage";
    }
    return "Unknown RtmpMessageType";
}

const char* messagetype2str(uint8_t t) {
    return messagetype2str((RtmpMessageType)t);
}

// Unchangable constants required by RTMP
static const uint32_t RTMP_INITIAL_CHUNK_SIZE = 128;
static const uint8_t RTMP_DEFAULT_VERSION = 3;
static const size_t RTMP_HANDSHAKE_SIZE0 = 1;
static const size_t RTMP_HANDSHAKE_SIZE1 = 1536;
static const size_t RTMP_HANDSHAKE_SIZE2 = RTMP_HANDSHAKE_SIZE1;
static const char* const SIMPLIFIED_RTMP_MAGIC_NUMBER = "BDMS";
static const size_t MAGIC_NUMBER_SIZE = 4; /* magic number */

// ========== The handshaking described in RTMP spec ==========

// The random data for handshaking
static butil::IOBuf* s_rtmp_handshake_server_random = NULL;
static pthread_once_t s_sr_once = PTHREAD_ONCE_INIT;
static void InitRtmpHandshakeServerRandom() {
    char buf[1528];
    for (int i = 0; i < 191; ++i) {
        ((uint64_t*)buf)[i] = butil::fast_rand();
    }
    s_rtmp_handshake_server_random = new butil::IOBuf;
    s_rtmp_handshake_server_random->append(buf, sizeof(buf));
}
static const butil::IOBuf& GetRtmpHandshakeServerRandom() {
    pthread_once(&s_sr_once, InitRtmpHandshakeServerRandom);
    return *s_rtmp_handshake_server_random;
}

static butil::IOBuf* s_rtmp_handshake_client_random = NULL;
static pthread_once_t s_cr_once = PTHREAD_ONCE_INIT;
static void InitRtmpHandshakeClientRandom() {
    char buf[1528];
    for (int i = 0; i < 191; ++i) {
        ((uint64_t*)buf)[i] = butil::fast_rand();
    }
    s_rtmp_handshake_client_random = new butil::IOBuf;
    s_rtmp_handshake_client_random->append(buf, sizeof(buf));
}
static const butil::IOBuf& GetRtmpHandshakeClientRandom() {
    pthread_once(&s_cr_once, InitRtmpHandshakeClientRandom);
    return *s_rtmp_handshake_client_random;
}

// For timestamps in simple handshaking.
static uint32_t GetRtmpTimestamp() {
    return 0;
}

// ========== Proprietary handshaking used by Adobe =========
namespace adobe_hs {

// Modified from code in SRS2 (src/protocol/srs_rtmp_handshake.cpp:94)
int openssl_HMACsha256(const void* key, int key_size,
                       const void* data, int data_size, void* digest) {
    if (NULL == EVP_sha256) {
        LOG_ONCE(ERROR) << "Fail to find EVP_sha256, fall back to simple handshaking";
        return -1;
    }
    unsigned int digest_size = 0;
    unsigned char* temp_digest = (unsigned char*)digest;
    if (key == NULL) {
        // NOTE: first parameter of EVP_Digest in older openssl is void*.
        if (EVP_Digest(const_cast<void*>(data), data_size, temp_digest,
                       &digest_size, EVP_sha256(), NULL) < 0) {
            LOG(ERROR) << "Fail to EVP_Digest";
            return -1;
        }
    } else {
        // Note: following code uses HMAC_CTX previously which is ABI
        // inconsistent in different version of openssl.
        if (HMAC(EVP_sha256(), key, key_size,
                 (const unsigned char*) data, data_size,
                 temp_digest, &digest_size) == NULL) {
            LOG(ERROR) << "Fail to HMAC";
            return -1;
        }
    }
    if (digest_size != 32) {
        LOG(ERROR) << "digest_size=" << digest_size << " of sha256 is not 32";
        return -1;
    }
    return 0;
}

// 68bytes FMS key for signing the sever packets.
static const uint8_t GenuineFMSKey[] = {
    0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
    0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
    0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
    0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
    0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
    0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
    0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
    0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
    0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
}; // 68
    
// 62bytes FlashPlayer key for signing the client packets.
static const uint8_t GenuineFPKey[] = {
    0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
    0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
    0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
    0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
    0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
    0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
    0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
    0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
}; // 62

static const uint32_t FP_VERSION = 0x80000702;
static const uint32_t FMS_VERSION = 0x01000504;

// A structure inside C1 or S1
class KeyBlock {
public:
    static const int SIZE = 764;
    static const int KEY_SIZE = 128;

    void Generate();
    void Load(const void* buf); // `buf' must be at least SIZE bytes
    void Save(void* buf) const; // ^

    const void* random0() const { return _buf; }
    int random0_size() const { return _offset; }
    const void* random1() const { return _buf + _offset + KEY_SIZE; }
    int random1_size() const { return SIZE - KEY_SIZE - 4 - _offset; }
    const void* key() const { return _buf + _offset; }
    void* key() { return _buf + _offset; }

private:
    uint32_t _offset;
    uint32_t _offset_data;
    char _buf[SIZE - 4];
};

// A structure inside C1 or S1
class DigestBlock {
public:
    static const int SIZE = 764;
    static const int DIGEST_SIZE = 32;

    void Generate();
    void Load(const void* buf); // `buf' must be at least SIZE bytes
    void Save(void* buf) const; // ^
    // `buf' must be at least SIZE - DIGEST_SIZE bytes
    void SaveWithoutDigest(void* buf) const;

    const void* random0() const { return _buf; }
    int random0_size() const { return _offset; }
    const void* random1() const { return _buf + _offset + DIGEST_SIZE; }
    int random1_size() const { return SIZE - DIGEST_SIZE - 4 - _offset; }
    const void* digest() const { return _buf + _offset; }
    void* digest() { return _buf + _offset; }

private:
    uint32_t _offset;
    uint32_t _offset_data;
    char _buf[SIZE - 4];
};

enum C1S1Schema { INVALID_SCHEMA, SCHEMA0, SCHEMA1 };

// Common part of C1 and S1.
class C1S1Base {
public:
    static const int SIZE = 1536;
    C1S1Base() : _schema(INVALID_SCHEMA) {}
    bool Save(void* buf) const;
    C1S1Schema schema() const { return _schema; }
protected:
    bool ComputeDigestBase(const void* key, int key_size, void* digest) const;
    C1S1Schema _schema;
public:
    uint32_t time;
    uint32_t version;
    KeyBlock key_blk;
    DigestBlock digest_blk;
};
    
class C1 : public C1S1Base {
public:
    bool Generate(C1S1Schema schema);
    bool Load(const void* buf);
protected:
    bool ComputeDigest(void* digest) const
    { return ComputeDigestBase(GenuineFPKey, 30, digest); }
};

class S1 : public C1S1Base {
public:
    bool Generate(const C1&);
    bool Load(const void* buf, C1S1Schema schema);
protected:
    bool ComputeDigest(void* digest) const
    { return ComputeDigestBase(GenuineFMSKey, 36, digest); }
};

// The common part of C2 and S2.
class C2S2Base {
public:
    static const int SIZE = 1536;
    static const int DIGEST_SIZE = 32;
    bool Generate(const void* key, int key_size, const void* c1s1_digest);
    bool Load(const void* key, int key_size,
              const void* c1s1_digest, const void* buf);
    void Save(void* buf) const;
private:
    bool ComputeDigest(const void* key, int key_size,
                       const void* c1s1_digest, void* digest) const;
public:
    char random[SIZE - DIGEST_SIZE];
    char digest[DIGEST_SIZE];
};

class C2 : public C2S2Base {
public:
    bool Generate(const void* s1_digest)
    { return C2S2Base::Generate(GenuineFPKey, 62, s1_digest); }
    bool Load(const void* s1_digest, const void* buf)
    { return C2S2Base::Load(GenuineFPKey, 62, s1_digest, buf); }
};

class S2 : public C2S2Base {
public:
    bool Generate(const void* c1_digest)
    { return C2S2Base::Generate(GenuineFMSKey, 68, c1_digest); }
    bool Load(const void* c1_digest, const void* buf)
    { return C2S2Base::Load(GenuineFMSKey, 68, c1_digest, buf); }
};

// ===== impl. =====
void KeyBlock::Generate() {
    _offset_data = butil::fast_rand() & 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)&_offset_data;
    _offset = ((uint32_t)p[0] + p[1] + p[2] + p[3]) % (SIZE - KEY_SIZE - 4);
    for (size_t i = 0; i < sizeof(_buf) / 8; ++i) {
        ((uint64_t*)_buf)[i] = butil::fast_rand();
    }
}

void KeyBlock::Load(const void* buf) {
    // Layout of key (764 bytes)
    //   random-data: `offset' bytes
    //   key-data: 128 bytes
    //   random-data: 764 - `offset' - 128 - 4 bytes
    //   offset: 4 bytes
    _offset_data = ReadBigEndian4Bytes((const char*)buf + SIZE - 4);
    const uint8_t* p = (const uint8_t*)&_offset_data;
    _offset = ((uint32_t)p[0] + p[1] + p[2] + p[3]) % (SIZE - KEY_SIZE - 4);
    memcpy(_buf, buf, SIZE - 4);
}

void KeyBlock::Save(void* buf) const {
    memcpy(buf, _buf, SIZE - 4);
    char* p = static_cast<char*>(buf) + SIZE - 4;
    WriteBigEndian4Bytes(&p, _offset_data);
}

void DigestBlock::Generate() {
    _offset_data = butil::fast_rand() & 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)&_offset_data;
    _offset = ((uint32_t)p[0] + p[1] + p[2] + p[3]) % (SIZE - DIGEST_SIZE - 4);
    for (size_t i = 0; i < sizeof(_buf) / 8; ++i) {
        ((uint64_t*)_buf)[i] = butil::fast_rand();
    }
}

void DigestBlock::Load(const void* buf) {
    // Layout of digest (764 bytes)
    //   offset: 4 bytes
    //   random-data: `offset' bytes
    //   digest-data: 32 bytes
    //   random-data: 764 - 4 - `offset' - 32 bytes
    _offset_data = ReadBigEndian4Bytes(buf);
    const uint8_t* p = (const uint8_t*)&_offset_data;
    _offset = ((uint32_t)p[0] + p[1] + p[2] + p[3]) % (SIZE - DIGEST_SIZE - 4);
    memcpy(_buf, (const char*)buf + 4, SIZE - 4);
}

void DigestBlock::Save(void* buf) const {
    char* p = static_cast<char*>(buf);
    WriteBigEndian4Bytes(&p, _offset_data);
    memcpy(p, _buf, SIZE - 4);
}

void DigestBlock::SaveWithoutDigest(void* buf) const {
    char* p = static_cast<char*>(buf);
    WriteBigEndian4Bytes(&p, _offset_data);
    memcpy(p, random0(), random0_size());
    p += random0_size();
    memcpy(p, random1(), random1_size());
}

bool C1S1Base::Save(void* buf) const {
    char* p = static_cast<char*>(buf);
    WriteBigEndian4Bytes(&p, time);
    WriteBigEndian4Bytes(&p, version);
    if (_schema == SCHEMA0) {
        key_blk.Save(p);
        digest_blk.Save(p + KeyBlock::SIZE);
        return true;
    } else if (_schema == SCHEMA1) {
        digest_blk.Save(p);
        key_blk.Save(p + DigestBlock::SIZE);
        return true;
    } else {
        CHECK(false) << "Invalid schema=" << (int)_schema;
        return false;
    }
}

bool C1S1Base::ComputeDigestBase(const void* key, int key_size,
                                 void* digest) const {
    char buf[SIZE - DigestBlock::DIGEST_SIZE];
    char* p = buf;
    WriteBigEndian4Bytes(&p, time);
    WriteBigEndian4Bytes(&p, version);
    if (_schema == SCHEMA0) {
        key_blk.Save(p);
        digest_blk.SaveWithoutDigest(p + KeyBlock::SIZE);
    } else if (_schema == SCHEMA1) {
        digest_blk.SaveWithoutDigest(p);
        key_blk.Save(p + DigestBlock::SIZE - DigestBlock::DIGEST_SIZE);
    } else {
        LOG(ERROR) << "Invalid schema=" << (int)_schema;
        return false;
    }
    char tmp_digest[EVP_MAX_MD_SIZE];
    if (openssl_HMACsha256(key, key_size, buf, sizeof(buf), tmp_digest) != 0) {
        LOG(WARNING) << "Fail to compute digest of C1/S1";
        return false;
    }
    memcpy(digest, tmp_digest, DigestBlock::DIGEST_SIZE);
    return true;
}

bool C1::Generate(C1S1Schema schema) {
    _schema = schema;
    time = ::time(NULL);
    version = FP_VERSION;
    key_blk.Generate();
    digest_blk.Generate();
    return ComputeDigest(digest_blk.digest());
}

bool C1::Load(const void* buf) {
    const uint8_t* const p = static_cast<const uint8_t*>(buf);
    time = ReadBigEndian4Bytes(p);
    version = ReadBigEndian4Bytes(p + 4);
    // Try schema0 (key before digest) first
    _schema = SCHEMA0;
    key_blk.Load(p + 8);
    digest_blk.Load(p + 8 + KeyBlock::SIZE);
    char tmp_digest[DigestBlock::DIGEST_SIZE];
    if (!ComputeDigest(tmp_digest)) {
        LOG(WARNING) << "Fail to compute digest of C1 (schema0)";
        return false;
    }
    if (memcmp(tmp_digest, digest_blk.digest(),
               DigestBlock::DIGEST_SIZE) == 0) {
        return true;
    }
    // Try schema1 (digest before key)
    _schema = SCHEMA1;
    digest_blk.Load(p + 8);
    key_blk.Load(p + 8 + DigestBlock::SIZE);
    if (!ComputeDigest(tmp_digest)) {
        LOG(WARNING) << "Fail to compute digest of C1 (schema1)";
        return false;
    }
    if (memcmp(tmp_digest, digest_blk.digest(),
               DigestBlock::DIGEST_SIZE) == 0) {
        return true;
    }
    _schema = INVALID_SCHEMA;
    return false;
}

bool S1::Generate(const C1& c1) {
    _schema = c1.schema();
    time = ::time(NULL);
    version = FMS_VERSION;
    key_blk.Generate();
    digest_blk.Generate();

    DHWrapper dh;
    if (dh.initialize(true) != 0) { // tool ~1.1ms
        return false;
    }
    int pkey_size = 128;
    if (dh.copy_shared_key(c1.key_blk.key(), 128,
                           key_blk.key(), &pkey_size) != 0) { // took ~0.9ms
        LOG(ERROR) << "Fail to compute key of S1";
        return false;
    }
    return ComputeDigest(digest_blk.digest());
}

bool S1::Load(const void* buf, C1S1Schema schema) {
    const uint8_t* const p = static_cast<const uint8_t*>(buf);
    _schema = schema;
    time = ReadBigEndian4Bytes(p);
    version = ReadBigEndian4Bytes(p + 4);
    if (_schema == SCHEMA0) {
        key_blk.Load(p + 8);
        digest_blk.Load(p + 8 + KeyBlock::SIZE);
    } else if (_schema == SCHEMA1) {
        digest_blk.Load(p + 8);
        key_blk.Load(p + 8 + DigestBlock::SIZE);
    }
    char tmp_digest[DigestBlock::DIGEST_SIZE];
    if (!ComputeDigest(tmp_digest)) {
        LOG(WARNING) << "Fail to compute digest of S1";
        return false;
    }
    if (memcmp(tmp_digest, digest_blk.digest(),
               DigestBlock::DIGEST_SIZE) != 0) {
        return false;
    }
    return true;
}

bool C2S2Base::ComputeDigest(const void* key, int key_size,
                             const void* c1s1_digest, void* digest) const {
    char temp_key[EVP_MAX_MD_SIZE];
    if (openssl_HMACsha256(key, key_size, c1s1_digest, DigestBlock::DIGEST_SIZE,
                           temp_key) != 0) {
        LOG(WARNING) << "Fail to create temp key";
        return false;
    }
    char tmp_digest[EVP_MAX_MD_SIZE];
    if (openssl_HMACsha256(temp_key, 32, random, SIZE - DIGEST_SIZE,
                           tmp_digest) != 0) {
        LOG(WARNING) << "Fail to create temp digest";
        return false;
    }
    memcpy(digest, tmp_digest, 32);
    return true;
}

bool C2S2Base::Generate(const void* key, int key_size,
                        const void* c1s1_digest) {
    for (int i = 0; i < (SIZE - DIGEST_SIZE) / 8; ++i) {
        ((uint64_t*)random)[i] = butil::fast_rand();
    }
    return ComputeDigest(key, key_size, c1s1_digest, digest);
}

bool C2S2Base::Load(const void* key, int key_size, const void* c1s1_digest,
                    const void* buf) {
    memcpy(random, buf, SIZE - DIGEST_SIZE);
    memcpy(digest, (const char*)buf + SIZE - DIGEST_SIZE, DIGEST_SIZE);
    char tmp_digest[DIGEST_SIZE];
    if (!ComputeDigest(key, key_size, c1s1_digest, tmp_digest)) {
        LOG(WARNING) << "Fail to compute digest of C2/S2";
        return false;
    }
    return memcmp(tmp_digest, digest, DIGEST_SIZE) == 0;
}

void C2S2Base::Save(void* buf) const {
    memcpy(buf, random, SIZE - DIGEST_SIZE);
    memcpy(static_cast<char*>(buf) + SIZE - DIGEST_SIZE, digest, DIGEST_SIZE);
}

} // namespace adobe_hs

// Get size of the basic header according to chunk stream id.
inline size_t GetBasicHeaderLength(uint32_t cs_id) {
    if (cs_id < 2) {
        return 0;
    } else if (cs_id <= 63) {
        return 1;
    } else if (cs_id <= 319) {
        return 2;
    } else if (cs_id <= RTMP_MAX_CHUNK_STREAM_ID) {
        return 3;
    } else {
        return 0;
    }
}

// Write a basic header into buf and forward the buf.
static void
WriteBasicHeader(char** buf, RtmpChunkType chunk_type, uint32_t cs_id) {
    char* out = *buf;
    if (cs_id < 2) {
        CHECK(false) << "Reserved chunk_stream_id=" << cs_id;
    } else if (cs_id <= 63) {
        *out++ = (((uint32_t)chunk_type << 6) | cs_id);
    } else if (cs_id <= 319) {
        *out++ = ((uint32_t)chunk_type << 6);
        *out++ = cs_id - 64;
    } else if (cs_id <= RTMP_MAX_CHUNK_STREAM_ID) {
        *out++ = (((uint32_t)chunk_type << 6) | 1);
        *out++ = (cs_id - 64) & 0xFF;
        *out++ = ((cs_id - 64) >> 8);
    } else {
        CHECK(false) << "Invalid chunk_stream_id=" << cs_id;
    }
    *buf = out;
}

// Write all data in *buf into fd.
// Returns 0 on success, -1 otherwise.
// Writing *all* is possible because we only call this fn during handshaking
// and connecting, the data in total is generally less than socket buffer.
static int WriteAll(int fd, butil::IOBuf* buf) {
    while (!buf->empty()) {
        ssize_t nw = buf->cut_into_file_descriptor(fd);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                // Almost impossible because we only call this fn during
                // RTMP handshaking/connecting, the total size that we may
                // write is less than buffer size of the fd. If the
                // impossible really happens, just spin until the fd becomes
                // writable.
                LOG_EVERY_SECOND(ERROR) << "Impossible: meet EAGAIN!";
                bthread_usleep(1000);
                continue;
            }
            return -1;
        }
    }
    return 0;
}

// Send C0 C1 to the socket.
// Used in rtmp.cpp
int SendC0C1(int fd, bool* is_simple_handshake) {
    bool done_adobe_hs = false;
    butil::IOBuf tmp;
    if (!FLAGS_rtmp_client_use_simple_handshake) {
        adobe_hs::C1 c1;
        if (c1.Generate(adobe_hs::SCHEMA1)) {
            char buf[RTMP_HANDSHAKE_SIZE0 + RTMP_HANDSHAKE_SIZE1];
            buf[0] = RTMP_DEFAULT_VERSION;
            c1.Save(buf + RTMP_HANDSHAKE_SIZE0);
            tmp.append(buf, sizeof(buf));
            done_adobe_hs = true;
        } else {
            LOG(WARNING) << "Fail to generate C1, use simple handshaking";
        }
    }
    if (is_simple_handshake) {
        *is_simple_handshake = !done_adobe_hs;
    }
    if (!done_adobe_hs) {
        char buf[9];
        char* p = buf;
        *p++ = RTMP_DEFAULT_VERSION; // c0 (version)
        WriteBigEndian4Bytes(&p, GetRtmpTimestamp()); // c1.time
        WriteBigEndian4Bytes(&p, 0); // c1.zero
        tmp.append(buf, sizeof(buf));
        tmp.append(GetRtmpHandshakeClientRandom());
    }
    return WriteAll(fd, &tmp);
}

const char* RtmpContext::state2str(State s) {
    switch (s) {
    case STATE_UNINITIALIZED: return "STATE_UNINITIALIZED";
    case STATE_RECEIVED_S0S1: return "STATE_RECEIVED_S0S1";
    case STATE_RECEIVED_S2:   return "STATE_RECEIVED_S2";
    case STATE_RECEIVED_C0C1: return "STATE_RECEIVED_C0C1";
    case STATE_RECEIVED_C2:   return "STATE_RECEIVED_C2";
    }
    return "Unknown state";
}

void RtmpContext::SetState(const butil::EndPoint& remote_side, State new_state) {
    const State old_state = _state;
    _state = new_state;
    RPC_VLOG << remote_side << ": " << state2str(old_state)
             << " -> " << state2str(new_state);
}

RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, uint32_t chunk_stream_id,
    const void* data, size_t n) {
    RtmpUnsentMessage* msg = new RtmpUnsentMessage;
    msg->header.message_length = n;
    msg->header.message_type = message_type;
    msg->header.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;
    msg->chunk_stream_id = chunk_stream_id;
    msg->body.append(data, n);
    return msg;
}

RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, const void* data, size_t n) {
    return MakeUnsentControlMessage(
        message_type, RTMP_CONTROL_CHUNK_STREAM_ID, data,  n);
}

RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, uint32_t chunk_stream_id, const butil::IOBuf& body) {
    RtmpUnsentMessage* msg = new RtmpUnsentMessage;
    msg->header.message_length = body.size();
    msg->header.message_type = message_type;
    msg->header.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;
    msg->chunk_stream_id = chunk_stream_id;
    msg->body = body;
    return msg;
}

RtmpUnsentMessage* MakeUnsentControlMessage(
    uint8_t message_type, const butil::IOBuf& body) {
    return MakeUnsentControlMessage(
        message_type, RTMP_CONTROL_CHUNK_STREAM_ID, body);
}

RtmpContext::RtmpContext(const RtmpClientOptions* copt, const Server* server)
    : _state(RtmpContext::STATE_UNINITIALIZED)
    , _s1_digest(NULL)
    , _chunk_size_out(RTMP_INITIAL_CHUNK_SIZE)
    , _chunk_size_in(RTMP_INITIAL_CHUNK_SIZE)
    , _window_ack_size(RTMP_DEFAULT_WINDOW_ACK_SIZE)
    , _nonack_bytes(0)
    , _received_bytes(0)
    , _cs_id_allocator(RTMP_CONTROL_CHUNK_STREAM_ID + 1)
    , _ms_id_allocator(RTMP_CONTROL_MESSAGE_STREAM_ID + 1)
    , _client_options(copt)
    , _on_connect(NULL)
    , _on_connect_arg(NULL)
    , _only_check_simple_s0s1(false)
    , _create_stream_with_play_or_publish(false)
    , _server(server)
    , _service(NULL)
    , _trans_id_allocator(2)
    , _simplified_rtmp(false) {
    if (server) {
        _service = server->options().rtmp_service;
    }
    _free_ms_ids.reserve(32);
    CHECK_EQ(0, _mstream_map.init(1024, 70));
    CHECK_EQ(0, _trans_map.init(1024, 70));
    memset(static_cast<void*>(_cstream_ctx), 0, sizeof(_cstream_ctx));
}
    
RtmpContext::~RtmpContext() {
    if (!_mstream_map.empty()) {
        size_t ncstream = 0;
        size_t nsstream = 0;
        for (butil::FlatMap<uint32_t, MessageStreamInfo>::iterator
                 it = _mstream_map.begin(); it != _mstream_map.end(); ++it) {
            if (it->second.stream->is_server_stream()) {
                ++nsstream;
            } else {
                ++ncstream;
            }
        }
        _mstream_map.clear();
        LOG(FATAL) << "RtmpContext=" << this << " is deallocated"
            " before all streams(" << ncstream << " client, " << nsstream
                   << "server) on the connection quit";
    }
    
    // cancel incomplete transactions
    for (butil::FlatMap<uint32_t, RtmpTransactionHandler*>::iterator
             it = _trans_map.begin(); it != _trans_map.end(); ++it) {
        if (it->second) {
            it->second->Cancel();
        }
    }
    _trans_map.clear();

    // Delete chunk streams
    for (size_t i = 0; i < RTMP_CHUNK_ARRAY_1ST_SIZE; ++i) {
        SubChunkArray* p = _cstream_ctx[i].load(butil::memory_order_relaxed);
        if (p) {
            _cstream_ctx[i].store(NULL, butil::memory_order_relaxed);
            delete p;
        }
    }

    free(_s1_digest);
    _s1_digest = NULL;
}

void RtmpContext::Destroy() {
    delete this;
}

butil::Status
RtmpUnsentMessage::AppendAndDestroySelf(butil::IOBuf* out, Socket* s) {
    std::unique_ptr<RtmpUnsentMessage> destroy_self(this);
    if (s == NULL) { // abandoned
        RPC_VLOG << "Socket=NULL";
        return butil::Status::OK();
    }
    RtmpContext* ctx = static_cast<RtmpContext*>(s->parsing_context());
    RtmpChunkStream* cstream = ctx->GetChunkStream(chunk_stream_id);
    if (cstream == NULL) {
        s->SetFailed(EINVAL, "Invalid chunk_stream_id=%u", chunk_stream_id);
        return butil::Status(EINVAL, "Invalid chunk_stream_id=%u", chunk_stream_id);
    }
    if (cstream->SerializeMessage(out, header, &body) != 0) {
        s->SetFailed(EINVAL, "Fail to serialize message");
        return butil::Status(EINVAL, "Fail to serialize message");
    }
    if (new_chunk_size) {
        ctx->_chunk_size_out = new_chunk_size;
    }
    if (!next) {
        return butil::Status::OK();
    }
    RtmpUnsentMessage* p = next.release();
    destroy_self.reset();
    return p->AppendAndDestroySelf(out, s);
}

RtmpContext::SubChunkArray::SubChunkArray() {
    memset(static_cast<void*>(ptrs), 0, sizeof(ptrs));
}

RtmpContext::SubChunkArray::~SubChunkArray() {
    for (size_t i = 0; i < RTMP_CHUNK_ARRAY_2ND_SIZE; ++i) {
        RtmpChunkStream* stream = ptrs[i].load(butil::memory_order_relaxed);
        if (stream) {
            ptrs[i].store(NULL, butil::memory_order_relaxed);
            delete stream;
        }
    }
}

RtmpChunkStream* RtmpContext::GetChunkStream(uint32_t cs_id) {
    if (cs_id > RTMP_MAX_CHUNK_STREAM_ID) {
        LOG(ERROR) << "Invalid chunk_stream_id=" << cs_id;
        return NULL;
    }
    const uint32_t index1 = cs_id / RTMP_CHUNK_ARRAY_2ND_SIZE;
    SubChunkArray* sub_array =
        _cstream_ctx[index1].load(butil::memory_order_consume);
    if (sub_array == NULL) {
        // Optimistic creation.
        sub_array = new SubChunkArray;
        SubChunkArray* expected = NULL;
        if (!_cstream_ctx[index1].compare_exchange_strong(
                expected, sub_array, butil::memory_order_acq_rel)) {
            delete sub_array;
            sub_array = expected;
        }
    }
    const uint32_t index2 = cs_id - index1 * RTMP_CHUNK_ARRAY_2ND_SIZE;
    RtmpChunkStream* cstream =
        sub_array->ptrs[index2].load(butil::memory_order_consume);
    if (cstream == NULL) {
        // Optimistic creation.
        cstream = new RtmpChunkStream(this, cs_id);
        RtmpChunkStream* expected = NULL;
        if (!sub_array->ptrs[index2].compare_exchange_strong(
                expected, cstream, butil::memory_order_acq_rel)) {
            delete cstream;
            cstream = expected;
        }
    }
    return cstream;
}

void RtmpContext::ClearChunkStream(uint32_t cs_id) {
    if (cs_id > RTMP_MAX_CHUNK_STREAM_ID) {
        LOG(ERROR) << "Invalid chunk_stream_id=" << cs_id;
        return;
    }
    const uint32_t index1 = cs_id / RTMP_CHUNK_ARRAY_2ND_SIZE;
    SubChunkArray* sub_array =
        _cstream_ctx[index1].load(butil::memory_order_consume);
    if (sub_array == NULL) {
        LOG(ERROR) << "chunk_stream_id=" << cs_id << " does not exist";
        return;
    }
    const uint32_t index2 = cs_id - index1 * RTMP_CHUNK_ARRAY_2ND_SIZE;
    RtmpChunkStream* cstream =
        sub_array->ptrs[index2].load(butil::memory_order_consume);
    if (cstream == NULL) {
        LOG(ERROR) << "chunk_stream_id=" << cs_id << " does not exist";
        return;
    }
    delete sub_array->ptrs[index2].exchange(
        NULL, butil::memory_order_acquire);
}

void RtmpContext::AllocateChunkStreamId(uint32_t* chunk_stream_id) {
    if (!_free_cs_ids.empty()) {
        *chunk_stream_id = _free_cs_ids.back();
        _free_cs_ids.pop_back();
        return;
    }
    *chunk_stream_id = _cs_id_allocator++;
    if (_cs_id_allocator > RTMP_MAX_CHUNK_STREAM_ID) {
        _cs_id_allocator = RTMP_CONTROL_CHUNK_STREAM_ID + 1;
    }
}

void RtmpContext::DeallocateChunkStreamId(uint32_t chunk_stream_id) {
    // NOTE: duplicated id may be pushed into _free_cs_ids, not affecting correctness.
    _free_cs_ids.push_back(chunk_stream_id);
}

bool RtmpContext::AllocateMessageStreamId(uint32_t* stream_id) {
    if (!_free_ms_ids.empty()) {
        *stream_id = _free_ms_ids.back();
        _free_ms_ids.pop_back();
        return true;
    }
    if (_ms_id_allocator != std::numeric_limits<uint32_t>::max()) {
        *stream_id = _ms_id_allocator++;
        return true;
    }
    return false;
}

void RtmpContext::DeallocateMessageStreamId(uint32_t stream_id) {
    _free_ms_ids.push_back(stream_id);
}

bool RtmpContext::FindMessageStream(
    uint32_t stream_id, butil::intrusive_ptr<RtmpStreamBase>* stream) {
    BAIDU_SCOPED_LOCK(_stream_mutex);
    MessageStreamInfo* info = _mstream_map.seek(stream_id);
    if (info == NULL || info->stream == NULL) {
        return false;
    }
    *stream = info->stream;
    return true;
}

bool RtmpContext::AddClientStream(RtmpStreamBase* stream) {
    const uint32_t stream_id = stream->stream_id();
    if (stream_id == RTMP_CONTROL_MESSAGE_STREAM_ID) {
        LOG(ERROR) << "stream_id=" << RTMP_CONTROL_MESSAGE_STREAM_ID
                   << " is reserved for control stream";
        return false;
    }
    uint32_t chunk_stream_id = 0;
    {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        MessageStreamInfo& info = _mstream_map[stream_id];
        if (info.stream != NULL) {
            mu.unlock();
            LOG(ERROR) << "stream_id=" << stream_id << " is already used";
            return false;
        }
        AllocateChunkStreamId(&chunk_stream_id);
        info.stream.reset(stream);
    }
    stream->_chunk_stream_id = chunk_stream_id;
    return true;
}

bool RtmpContext::AddServerStream(RtmpStreamBase* stream) {
    uint32_t stream_id = 0;
    {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        if (!AllocateMessageStreamId(&stream_id)) {
            return false;
        }
        MessageStreamInfo& info = _mstream_map[stream_id];
        if (info.stream != NULL) {
            mu.unlock();
            LOG(ERROR) << "stream_id=" << stream_id << " is already used";
            return false;
        }
        info.stream.reset(stream);
    }
    stream->_message_stream_id = stream_id;
    stream->_chunk_stream_id = 0;
    return true;
}

bool RtmpContext::RemoveMessageStream(RtmpStreamBase* stream) {
    if (stream == NULL) {
        LOG(FATAL) << "Param[stream] is NULL";
        return false;
    }
    const uint32_t stream_id = stream->stream_id();
    if (stream_id == RTMP_CONTROL_MESSAGE_STREAM_ID) {
        LOG(FATAL) << "stream_id=" << RTMP_CONTROL_MESSAGE_STREAM_ID
                   << " is reserved for control stream";
        return false;
    }
    // for deref the stream outside _stream_mutex.
    butil::intrusive_ptr<RtmpStreamBase> deref_ptr; 
    {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        MessageStreamInfo* info = _mstream_map.seek(stream_id);
        if (info == NULL) {
            mu.unlock();
            return false;
        }
        if (stream != info->stream) {
            mu.unlock();
            LOG(FATAL) << "Unmatched "
                       << (stream->is_client_stream() ? "client" : "server")
                       << " stream of stream_id=" << stream_id;
            return false;
        }
        if (stream->is_client_stream()) {
            DeallocateChunkStreamId(stream->_chunk_stream_id);
        } else { // server-side
            DeallocateMessageStreamId(stream_id);
        }
        deref_ptr.swap(info->stream);
        _mstream_map.erase(stream_id);
    }
    return true;
}

// The transactionID is for createStream and RPC-call in RTMP which is
// infrequent in most cases, thus we just use a map protected with a mutex
// to allocate the id. To lower the possibility of ABA issues, we increase
// transaction id on each allocation. To jump over crowded area fastly, we
// double the increment of `_trans_id_allocator' on each try.
bool RtmpContext::AddTransaction(uint32_t* out_transaction_id,
                                 RtmpTransactionHandler* handler) {
    uint32_t transaction_id = 0;
    uint32_t step = 1;
    BAIDU_SCOPED_LOCK(_trans_mutex);
    for (int i = 0; i < 10; ++i) {
        transaction_id = _trans_id_allocator;
        _trans_id_allocator += step;
        if (transaction_id < 2) { // 0 and 1 are reserved by rtmp spec.
            continue;
        }
        step *= 2; // 1,2,4,8,16,32,64,128,256,512,1024
        if (_trans_map.seek(transaction_id) == NULL) {
            _trans_map[transaction_id] = handler;
            *out_transaction_id = transaction_id;
            return true;
        }
    }
    return false;
}

RtmpTransactionHandler*
RtmpContext::RemoveTransaction(uint32_t transaction_id) {
    RtmpTransactionHandler* handler = NULL;
    {
        BAIDU_SCOPED_LOCK(_trans_mutex);
        RtmpTransactionHandler** phandler = _trans_map.seek(transaction_id);
        if (phandler != NULL) {
            handler = *phandler;
            _trans_map.erase(transaction_id);
        }
    }
    return handler;
}

int RtmpContext::SendConnectRequest(const butil::EndPoint& remote_side, int fd, bool simplified_rtmp) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_CONNECT, &ostream);
        WriteAMFUint32(1, &ostream);
        RtmpConnectRequest req;
        if (_client_options->app.empty()) {
            LOG(ERROR) << "RtmpClientOptions.app must be set";
            return -1;
        }
        req.set_app(_client_options->app);
        if (!_client_options->flashVer.empty()) {
            req.set_flashver(_client_options->flashVer);
        }
        if (!_client_options->swfUrl.empty()) {
            req.set_swfurl(_client_options->swfUrl);
        }
        // formulate tcUrl
        if (_client_options->tcUrl.empty()) {
            std::string* const tcurl = req.mutable_tcurl();
            tcurl->reserve(32 + _client_options->app.size());
            tcurl->append("rtmp://");
            tcurl->append(butil::endpoint2str(remote_side).c_str());
            tcurl->push_back('/');
            tcurl->append(_client_options->app);
        } else {
            req.set_tcurl(_client_options->tcUrl);
        }
        req.set_fpad(_client_options->fpad);
        req.set_capabilities(239); // Copy from  SRS
        req.set_audiocodecs(_client_options->audioCodecs);
        req.set_videocodecs(_client_options->videoCodecs);
        req.set_videofunction(_client_options->videoFunction);
        if (!_client_options->pageUrl.empty()) {
            req.set_pageurl(_client_options->pageUrl);
        }
        req.set_objectencoding(RTMP_AMF0);
        req.set_stream_multiplexing(true);
        WriteAMFObject(req, &ostream);
        if (!ostream.good()) {
            LOG(ERROR) << "Fail to serialize connect request";
            return -1;
        }
    }
    RtmpMessageHeader header;
    header.message_length = req_buf.size();
    header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    header.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;

    butil::IOBuf msg_buf;
    if (simplified_rtmp) {
        char buf[5];
        char* p = buf;
        *p++ = RTMP_DEFAULT_VERSION;
        memcpy(p, SIMPLIFIED_RTMP_MAGIC_NUMBER, MAGIC_NUMBER_SIZE);
        msg_buf.append(buf, sizeof(buf));
    }
    RtmpChunkStream* cstream = GetChunkStream(RTMP_CONTROL_CHUNK_STREAM_ID);
    if (cstream->SerializeMessage(&msg_buf, header, &req_buf) != 0) {
        LOG(ERROR) << "Fail to serialize connect message";
        return -1;
    }

    // WindowAckSize
    {
        char cntl_buf[4];
        char* p = cntl_buf;
        WriteBigEndian4Bytes(&p, _client_options->window_ack_size);
        RtmpMessageHeader header2;
        header2.message_length = sizeof(cntl_buf);
        header2.message_type = RTMP_MESSAGE_WINDOW_ACK_SIZE;
        header2.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;
        butil::IOBuf tmp;
        tmp.append(cntl_buf, sizeof(cntl_buf));
        if (cstream->SerializeMessage(&msg_buf, header2, &tmp) != 0) {
            LOG(ERROR) << "Fail to serialize WindowAckSize message";
            return -1;
        }
    }

    // SetChunkSize
    {
        char cntl_buf[4];
        char* p = cntl_buf;
        WriteBigEndian4Bytes(&p, _client_options->chunk_size);
        RtmpMessageHeader header3;
        header3.message_length = sizeof(cntl_buf);
        header3.message_type = RTMP_MESSAGE_SET_CHUNK_SIZE;
        header3.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;
        butil::IOBuf tmp;
        tmp.append(cntl_buf, sizeof(cntl_buf));
        if (cstream->SerializeMessage(&msg_buf, header3, &tmp) != 0) {
            LOG(ERROR) << "Fail to serialize SetChunkSize message";
            return -1;
        }
        _chunk_size_out = _client_options->chunk_size;
    }
    
    return WriteAll(fd, &msg_buf);
}

ParseResult RtmpContext::Feed(butil::IOBuf* source, Socket* socket) {
    switch (_state) {
    case STATE_UNINITIALIZED:
        if (socket->CreatedByConnect()) {
            return WaitForS0S1(source, socket);
        } else {
            return WaitForC0C1orSimpleRtmp(source, socket);
        }
    case STATE_RECEIVED_S0S1:
        return WaitForS2(source, socket);
    case STATE_RECEIVED_C0C1:
        return WaitForC2(source, socket);
    case STATE_RECEIVED_S2:
    case STATE_RECEIVED_C2:
        return OnChunks(source, socket);
    }
    CHECK(false) << "Never here!";
    return MakeParseError(PARSE_ERROR_NO_RESOURCE);
}

ParseResult RtmpContext::WaitForC0C1orSimpleRtmp(butil::IOBuf* source, Socket* socket) {
    if (source->length() < RTMP_HANDSHAKE_SIZE0 + MAGIC_NUMBER_SIZE) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    char buf[RTMP_HANDSHAKE_SIZE0 + MAGIC_NUMBER_SIZE];
    const char* p = (const char*)source->fetch(buf, sizeof(buf));
    if (memcmp(p + RTMP_HANDSHAKE_SIZE0, SIMPLIFIED_RTMP_MAGIC_NUMBER, MAGIC_NUMBER_SIZE) == 0) {
        source->pop_front(RTMP_HANDSHAKE_SIZE0 + MAGIC_NUMBER_SIZE);
        SetState(socket->remote_side(), STATE_RECEIVED_C2);
        _simplified_rtmp = true;
        return OnChunks(source, socket);
    }

    if (source->length() < RTMP_HANDSHAKE_SIZE0 + RTMP_HANDSHAKE_SIZE1) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    // Consume C0C1 and change the state.
    char c0c1_buf[RTMP_HANDSHAKE_SIZE0 + RTMP_HANDSHAKE_SIZE1];
    source->cutn(c0c1_buf, sizeof(c0c1_buf));
    SetState(socket->remote_side(), STATE_RECEIVED_C0C1);

    butil::IOBuf tmp;
    adobe_hs::C1 c1;
    if (c1.Load(c0c1_buf + RTMP_HANDSHAKE_SIZE0)) {
        RPC_VLOG << socket->remote_side() << ": Loaded C1 with schema"
                 << (c1.schema() == adobe_hs::SCHEMA0 ? "0" : "1");
        tmp.push_back(RTMP_DEFAULT_VERSION);
        {
            adobe_hs::S1 s1;
            if (!s1.Generate(c1)) {
                LOG(WARNING) << socket->remote_side() << ": Fail to generate s1";
                return MakeParseError(PARSE_ERROR_NO_RESOURCE);
            }
            char buf[RTMP_HANDSHAKE_SIZE1];
            s1.Save(buf);
            tmp.append(buf, RTMP_HANDSHAKE_SIZE1);
            _s1_digest = malloc(adobe_hs::DigestBlock::DIGEST_SIZE);
            if (_s1_digest == NULL) {
                LOG(ERROR) << "Fail to malloc";
                return MakeParseError(PARSE_ERROR_NO_RESOURCE);
            }
            memcpy(_s1_digest, s1.digest_blk.digest(),
                   adobe_hs::DigestBlock::DIGEST_SIZE);
        }
        {
            adobe_hs::S2 s2;
            if (!s2.Generate(c1.digest_blk.digest())) {
                LOG(ERROR) << socket->remote_side() << ": Fail to generate s2";
                return MakeParseError(PARSE_ERROR_NO_RESOURCE);
            }
            char buf[RTMP_HANDSHAKE_SIZE2];
            s2.Save(buf);
            tmp.append(buf, RTMP_HANDSHAKE_SIZE2);
        }
    } else {
        RPC_VLOG << socket->remote_side() << ": Fallback to simple handshaking";
        // Send back S0 S1 S2
        char buf[9];
        char* q = buf;
        *q++ = RTMP_DEFAULT_VERSION; // s0 (version)
        WriteBigEndian4Bytes(&q, GetRtmpTimestamp()); // s1.time
        WriteBigEndian4Bytes(&q, 0); // s1.zero
        tmp.append(buf, sizeof(buf));
        tmp.append(GetRtmpHandshakeServerRandom());
    
        char* const s2 = c0c1_buf + RTMP_HANDSHAKE_SIZE0;
        q = s2 + 4;
        const uint32_t s2_time2 = GetRtmpTimestamp();
        WriteBigEndian4Bytes(&q, s2_time2);
        tmp.append(s2, RTMP_HANDSHAKE_SIZE2);
    }
    if (WriteAll(socket->fd(), &tmp) != 0) {
        LOG(WARNING) << socket->remote_side() << ": Fail to write S0 S1 S2";
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    return WaitForC2(source, socket);
}
    
ParseResult RtmpContext::WaitForC2(butil::IOBuf* source, Socket* socket) {
    if (source->length() < RTMP_HANDSHAKE_SIZE2) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    // Consume C2 and change the state.
    char c2_buf[RTMP_HANDSHAKE_SIZE2];
    source->cutn(c2_buf, sizeof(c2_buf));
    // C2 loading is so common to be failed (for players based on flash)
    // Don't load C2 right now.
    // if (_s1_digest) {
    //     adobe_hs::C2 c2;
    //     if (!c2.Load(_s1_digest, c2_buf)) {
    //         LOG(WARNING) << socket->remote_side() << ": Fail to load C2";
    //     }
    // }
    SetState(socket->remote_side(), STATE_RECEIVED_C2);
    return OnChunks(source, socket);
}

ParseResult RtmpContext::WaitForS0S1(butil::IOBuf* source, Socket* socket) {
    if (source->length() < RTMP_HANDSHAKE_SIZE0 + RTMP_HANDSHAKE_SIZE1) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    // Consume S0 S1 and change the state.
    char s0s1_buf[RTMP_HANDSHAKE_SIZE0 + RTMP_HANDSHAKE_SIZE1];
    source->cutn(s0s1_buf, sizeof(s0s1_buf));
    SetState(socket->remote_side(), STATE_RECEIVED_S0S1);

    butil::IOBuf tmp;
    bool done_adobe_hs = false;
    if (!_only_check_simple_s0s1) {
        adobe_hs::S1 s1;
        if (s1.Load(s0s1_buf + RTMP_HANDSHAKE_SIZE0, adobe_hs::SCHEMA1)) {
            RPC_VLOG << socket->remote_side() << ": Loaded S1 with schema1";
            adobe_hs::C2 c2;
            if (!c2.Generate(s1.digest_blk.digest())) {
                LOG(ERROR) << socket->remote_side() << ": Fail to generate c2";
                return MakeParseError(PARSE_ERROR_NO_RESOURCE);
            }
            c2.Save(s0s1_buf + RTMP_HANDSHAKE_SIZE0);
            tmp.append(s0s1_buf + RTMP_HANDSHAKE_SIZE0, RTMP_HANDSHAKE_SIZE1);
            done_adobe_hs = true;
        } else {
            RPC_VLOG << socket->remote_side() << ": Fallback to simple handshaking";
        }
    }
    if (!done_adobe_hs) {
        // Send back C2
        char* const c2 = s0s1_buf + RTMP_HANDSHAKE_SIZE0;
        char* q = c2 + 4;  // skip time
        const uint32_t c2_time2 = GetRtmpTimestamp();
        WriteBigEndian4Bytes(&q, c2_time2); // time2
        tmp.append(c2, RTMP_HANDSHAKE_SIZE2);
    }
    if (WriteAll(socket->fd(), &tmp) != 0) {
        LOG(WARNING) << socket->remote_side() << ": Fail to write C2";
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    return WaitForS2(source, socket);
}
    
ParseResult RtmpContext::WaitForS2(butil::IOBuf* source, Socket* socket) {
    if (source->length() < RTMP_HANDSHAKE_SIZE2) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    // Consume S2 and change the state.
    source->pop_front(RTMP_HANDSHAKE_SIZE2);
    SetState(socket->remote_side(), STATE_RECEIVED_S2);

    if (SendConnectRequest(socket->remote_side(), socket->fd(), false) != 0) {
        LOG(ERROR) << "Fail to send connect request to " << socket->remote_side();
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    return OnChunks(source, socket);
}

ParseResult RtmpContext::OnChunks(butil::IOBuf* source, Socket* socket) {
    // Parse basic header.
    const char* p = (const char*)source->fetch1();
    if (NULL == p) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const uint8_t first_byte = *p;
    // 2 bits, deciding type of following chunk message header.
    const RtmpChunkType fmt = (RtmpChunkType)(first_byte >> 6);
    // cs_id is short for "chunk stream id"
    uint32_t cs_id = (first_byte & 0x3F);
    uint8_t basic_header_len = 1u;
    if (cs_id == 0) { // 2-byte basic header
        if (source->size() < 2u) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        char basic_header_buf[2];
        const uint8_t* p = (const uint8_t*)source->fetch(basic_header_buf, 2);
        cs_id = ((uint32_t)p[1]) + 64;
        basic_header_len = 2u;
    } else if (cs_id == 1) { // 3-byte basic header
        if (source->size() < 3u) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        char basic_header_buf[3];
        const uint8_t* p = (const uint8_t*)source->fetch(basic_header_buf, 3);
        cs_id = ((uint32_t)p[2]) * 256 + ((uint32_t)p[1]) + 64;
        basic_header_len = 3u;
    } // else 1-byte basic header, keep cs_id as it is.
    RtmpBasicHeader bh = { cs_id, fmt, basic_header_len };
    RtmpChunkStream* cstream = GetChunkStream(cs_id);
    if (cstream == NULL) {
        LOG(ERROR) << "Invalid chunk_stream_id=" << cs_id;
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    return cstream->Feed(bh, source, socket);
}

static int SendAck(Socket* socket, uint64_t received_bytes) {
    const uint32_t data = butil::HostToNet32(received_bytes);
    SocketMessagePtr<RtmpUnsentMessage> msg(
        MakeUnsentControlMessage(RTMP_MESSAGE_ACK, &data, sizeof(data)));
    return WriteWithoutOvercrowded(socket, msg);
}

void RtmpContext::AddReceivedBytes(Socket* socket, uint32_t sz) {
    _received_bytes += sz;
    _nonack_bytes += sz;
    if (_nonack_bytes > _window_ack_size) {
        _nonack_bytes -= _window_ack_size;
        PLOG_IF(WARNING, SendAck(socket, _received_bytes) != 0)
            << socket->remote_side() << ": Fail to send ack";
    }
}

// ============ RtmpChunkStream ==============

RtmpChunkStream::RtmpChunkStream(RtmpContext* conn_ctx, uint32_t cs_id)
    : _conn_ctx(conn_ctx)
    , _cs_id(cs_id) {
}

RtmpChunkStream::ReadParams::ReadParams() 
    : last_has_extended_ts(false)
    , first_chunk_of_message(true)
    , last_timestamp_delta(0)
    , left_message_length(0) {
}

RtmpChunkStream::WriteParams::WriteParams() 
    : last_has_extended_ts(false)
    , last_timestamp_delta(0) {
}

MethodStatus* g_client_msg_status = NULL;
static pthread_once_t g_client_msg_status_once = PTHREAD_ONCE_INIT;
static void InitClientMessageStatus() {
    g_client_msg_status = new MethodStatus;
    g_client_msg_status->Expose("rtmp_client_in");
}

MethodStatus* g_server_msg_status = NULL;
static pthread_once_t g_server_msg_status_once = PTHREAD_ONCE_INIT;
static void InitServerMessageStatus() {
    g_server_msg_status = new MethodStatus;
    g_server_msg_status->Expose("rtmp_server_in");
}

struct ChunkStatus {
    bvar::Adder<int64_t> count;
    bvar::PerSecond<bvar::Adder<int64_t> > second;
    ChunkStatus() : second("rtmp_chunk_in_second", &count) {}
};
inline void AddChunk() {
    butil::get_leaky_singleton<ChunkStatus>()->count << 1;
}

ParseResult RtmpChunkStream::Feed(const RtmpBasicHeader& bh,
                                  butil::IOBuf* source, Socket* socket) {
    // Parse message header. Notice that basic header is still in source.
    uint32_t header_len = bh.header_length;
    bool has_extended_ts = false;
    uint32_t timestamp_delta = 0;
    RtmpMessageHeader mh;
    uint32_t cur_chunk_size = 0;
    RtmpContext* ctx = connection_context();
    const uint32_t chunk_size_in = ctx->_chunk_size_in;
        
    switch (bh.fmt) {
    case RTMP_CHUNK_TYPE0: {
        // Type 0 chunk headers are 11 bytes long. This type MUST be
        // used at the start of a chunk stream, and whenever the stream
        // timestamp goes backward (e.g., because of a backward seek).
        const uint32_t MSG_HEADER_LEN = 11u;
        header_len += MSG_HEADER_LEN;
        if (source->size() < header_len) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        char buf[header_len + 4/*extended ts*/];
        const char* p = (const char*)source->fetch(buf, header_len)
            + bh.header_length;
        mh.timestamp = ReadBigEndian3Bytes(p);
        if (mh.timestamp == 0xFFFFFFu) {
            header_len += 4u;
            if (source->size() < header_len) {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            p = (const char*)source->fetch(buf, header_len)
                + bh.header_length; // fetch again.
            mh.timestamp = ReadBigEndian4Bytes(p + MSG_HEADER_LEN);
            has_extended_ts = true;
        }
        timestamp_delta = mh.timestamp;
        mh.message_length = ReadBigEndian3Bytes(p + 3);
        _r.left_message_length = mh.message_length;
        cur_chunk_size = std::min(chunk_size_in, _r.left_message_length);
        if (source->size() < header_len + cur_chunk_size) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        _r.left_message_length -= cur_chunk_size;
        mh.message_type = p[6];
            
        // NOTE: stream_id is little endian.
        char* pmsid = (char*)&mh.stream_id;
        pmsid[0] = p[7];
        pmsid[1] = p[8];
        pmsid[2] = p[9];
        pmsid[3] = p[10];
    } break;
    case RTMP_CHUNK_TYPE1: {
        // Type 1 chunk headers are 7 bytes long. The message stream ID is
        // not included; this chunk takes the same stream ID as the
        // preceding chunk. Streams with variable-sized messages
        // (for example, many video formats) SHOULD use this format for
        // the first chunk of each new message after the first.
        const uint32_t MSG_HEADER_LEN = 7u;
        header_len += MSG_HEADER_LEN;
        if (source->size() < header_len) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        char buf[header_len + 4/*extended ts*/];
        const char* p = (const char*)source->fetch(buf, header_len)
            + bh.header_length;
        timestamp_delta = ReadBigEndian3Bytes(p);
        if (timestamp_delta == 0xFFFFFFu) {
            header_len += 4u;
            if (source->size() < header_len) {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            p = (const char*)source->fetch(buf, header_len)
                + bh.header_length; // fetch again.
            timestamp_delta = ReadBigEndian4Bytes(p + MSG_HEADER_LEN);
            has_extended_ts = true;
        }
        if (!_r.last_msg_header.is_valid()) {
            LOG(ERROR) << "No last message in chunk_stream=" << _cs_id
                       << " for ChunkType1";
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        mh.timestamp = _r.last_msg_header.timestamp + timestamp_delta;
        mh.message_length = ReadBigEndian3Bytes(p + 3);
        _r.left_message_length = mh.message_length;
        cur_chunk_size = std::min(chunk_size_in, _r.left_message_length);
        if (source->size() < header_len + cur_chunk_size) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        _r.left_message_length -= cur_chunk_size;
        mh.message_type = p[6];
        mh.stream_id = _r.last_msg_header.stream_id;
    } break;
    case RTMP_CHUNK_TYPE2: {
        // Type 2 chunk headers are 3 bytes long. Neither the stream ID
        // nor the message length is included; this chunk has the same
        // stream ID and message length as the preceding chunk. Streams
        // with constant-sized messages (for example, some audio and data
        // formats) SHOULD use this format for the first chunk of each
        // message after the first.
        const uint32_t MSG_HEADER_LEN = 3u;
        header_len += MSG_HEADER_LEN;
        if (source->size() < header_len) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        char buf[header_len + 4/*extended ts*/];
        const char* p = (const char*)source->fetch(buf, header_len)
            + bh.header_length;
        timestamp_delta = ReadBigEndian3Bytes(p);
        if (timestamp_delta == 0xFFFFFFu) {
            header_len += 4u;
            if (source->size() < header_len) {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            p = (const char*)source->fetch(buf, header_len)
                + bh.header_length; // fetch again.
            timestamp_delta = ReadBigEndian4Bytes(p + MSG_HEADER_LEN);
            has_extended_ts = true;
        }
        if (!_r.last_msg_header.is_valid()) {
            LOG(ERROR) << "No last message in chunk_stream=" << _cs_id
                       << " for ChunkType2";
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        mh.timestamp = _r.last_msg_header.timestamp + timestamp_delta;
        mh.message_length = _r.last_msg_header.message_length;
        cur_chunk_size = std::min(chunk_size_in, _r.left_message_length);
        if (source->size() < header_len + cur_chunk_size) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        _r.left_message_length -= cur_chunk_size;
        mh.message_type = _r.last_msg_header.message_type;
        mh.stream_id = _r.last_msg_header.stream_id;
    } break;
    case RTMP_CHUNK_TYPE3: {
        // Type 3 chunks have no message header, everything inherits from
        // previous chunks.
        if (!_r.last_has_extended_ts) {
            timestamp_delta = _r.last_timestamp_delta;
        } else {
            header_len += 4u;
            if (source->size() < header_len) {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            char buf[header_len];
            const char* p = (const char*)source->fetch(buf, header_len)
                + bh.header_length;
            timestamp_delta = ReadBigEndian4Bytes(p);
            has_extended_ts = true;
            // librtmp may not set extended timestamp for non-first type-3
            // chunks of a message. Assume that the extended timestamp exists,
            // if the timestamp does not match the ones in previous chunks
            // (of the message), rewind the parsing cursor.
            if (!_r.first_chunk_of_message &&
                timestamp_delta > 0 &&
                timestamp_delta != _r.last_timestamp_delta) {
                header_len -= 4;
                timestamp_delta = _r.last_timestamp_delta;
            }
        }
        if (!_r.last_msg_header.is_valid()) {
            LOG(ERROR) << "No last message in chunk_stream=" << _cs_id
                       << " for ChunkType3";
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        mh.timestamp = _r.last_msg_header.timestamp;
        if (_r.first_chunk_of_message) {
            // Only the first type-3 chunk adds the delta.
            mh.timestamp += timestamp_delta;
        }
        mh.message_length = _r.last_msg_header.message_length;
        cur_chunk_size = std::min(chunk_size_in, _r.left_message_length);
        if (source->size() < header_len + cur_chunk_size) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        _r.left_message_length -= cur_chunk_size;
        mh.message_type = _r.last_msg_header.message_type;
        mh.stream_id = _r.last_msg_header.stream_id;
    } break;
    } // switch(fmt)

    source->pop_front(header_len);
    source->cutn(&_r.msg_body, cur_chunk_size);
    ctx->AddReceivedBytes(socket, header_len + cur_chunk_size);
    const int vlvl = RPC_VLOG_LEVEL + 2;
    VLOG(vlvl) << socket->remote_side()
              << ": Chunk{chunk_stream_id=" << bh.chunk_stream_id
              << " fmt=" << bh.fmt
              << " body_size=" << cur_chunk_size << '}';
    _r.last_has_extended_ts = has_extended_ts;
    _r.last_timestamp_delta = timestamp_delta;
    _r.last_msg_header = mh;
    
    AddChunk();

    if (_r.left_message_length == 0) {
        MethodStatus* st = NULL;
        if (ctx->service() != NULL) {
            pthread_once(&g_server_msg_status_once, InitServerMessageStatus);
            st = g_server_msg_status;
        } else {
            pthread_once(&g_client_msg_status_once, InitClientMessageStatus);
            st = g_client_msg_status;
        }
        if (st) {
            butil::Timer tm;
            tm.start();
            CHECK(st->OnRequested());
            const bool ret = OnMessage(bh, mh, &_r.msg_body, socket);
            tm.stop();
            st->OnResponded(ret, tm.u_elapsed());
        } else {
            (void)OnMessage(bh, mh, &_r.msg_body, socket);
        }
        _r.msg_body.clear();
        _r.left_message_length = mh.message_length;
        _r.first_chunk_of_message = true;
    } else {
        _r.first_chunk_of_message = false;
    }
    return MakeMessage(NULL);
}

int RtmpChunkStream::SerializeMessage(butil::IOBuf* buf,
                                      const RtmpMessageHeader& mh,
                                      butil::IOBuf* body) {
    const size_t bh_size = GetBasicHeaderLength(_cs_id);
    if (bh_size == 0) {
        CHECK(false) << "Invalid chunk_stream_id=" << _cs_id;
        return -1;
    }
    // NOTE: body->size() may be longer than mh.message_length;
    uint32_t left_size = mh.message_length;
    CHECK_LE((size_t)left_size, body->size());
    bool has_extended_ts = false;
    uint32_t timestamp_delta = 0;
    const uint32_t chunk_size_out = connection_context()->_chunk_size_out;
    if (left_size > 0) {
        const uint32_t cur_chunk_size = std::min(chunk_size_out, left_size);
        left_size -= cur_chunk_size;
        char header_buf[32]; // enough
        char* p = header_buf + bh_size;
        RtmpChunkType chunk_type = RTMP_CHUNK_TYPE0;
        if (!_w.last_msg_header.is_valid() ||
            mh.stream_id != _w.last_msg_header.stream_id ||
            mh.timestamp < _w.last_msg_header.timestamp) { // backward seek
            chunk_type = RTMP_CHUNK_TYPE0;
            timestamp_delta = mh.timestamp;
            uint32_t packed_ts = mh.timestamp;
            if (packed_ts >= 0xFFFFFFu) {
                has_extended_ts = true;
                packed_ts = 0xFFFFFFu;
            }
            WriteBigEndian3Bytes(&p, packed_ts);
            WriteBigEndian3Bytes(&p, mh.message_length);
            *p++ = mh.message_type;
            WriteLittleEndian4Bytes(&p, mh.stream_id);
        } else if (mh.message_length  != _w.last_msg_header.message_length ||
                   mh.message_type != _w.last_msg_header.message_type) {
            chunk_type = RTMP_CHUNK_TYPE1;
            timestamp_delta = mh.timestamp - _w.last_msg_header.timestamp;
            uint32_t packed_ts = timestamp_delta;
            if (packed_ts >= 0xFFFFFFu) {
                has_extended_ts = true;
                packed_ts = 0xFFFFFFu;
            }
            WriteBigEndian3Bytes(&p, packed_ts);
            WriteBigEndian3Bytes(&p, mh.message_length);
            *p++ = mh.message_type;
        } else {
            timestamp_delta = mh.timestamp - _w.last_msg_header.timestamp;
            if (timestamp_delta != _w.last_timestamp_delta) {
                chunk_type = RTMP_CHUNK_TYPE2;
                uint32_t packed_ts = timestamp_delta;
                if (packed_ts >= 0xFFFFFFu) {
                    has_extended_ts = true;
                    packed_ts = 0xFFFFFFu;
                }
                WriteBigEndian3Bytes(&p, packed_ts);
            } else {
                chunk_type = RTMP_CHUNK_TYPE3;
                has_extended_ts = _w.last_has_extended_ts;
            }
        }
        if (has_extended_ts) {
            WriteBigEndian4Bytes(&p, timestamp_delta);
        }
        // Overwrite the basic header again (with correct chunk_type).
        char* dummy_ptr = header_buf;
        WriteBasicHeader(&dummy_ptr, chunk_type, _cs_id);
        buf->append(header_buf, p - header_buf);
        body->cutn(buf, cur_chunk_size);
        
        _w.last_has_extended_ts = has_extended_ts;
        _w.last_timestamp_delta = timestamp_delta;
        _w.last_msg_header = mh;
    }
    // Send left data as type-3 chunks
    while (left_size > 0) {
        const uint32_t cur_chunk_size = std::min(chunk_size_out, left_size);
        left_size -= cur_chunk_size;
        has_extended_ts = _w.last_has_extended_ts;
        char header_buf[8]; // enough (3+4 bytes at maximum)
        char* p = header_buf;
        WriteBasicHeader(&p, RTMP_CHUNK_TYPE3, _cs_id);
        if (has_extended_ts) {
            // Add extended timestamp in non-first type-3 chunks to be
            // consistent with flash/FMLE/FMS.
            WriteBigEndian4Bytes(&p, timestamp_delta);
        }
        buf->append(header_buf, p - header_buf);
        body->cutn(buf, cur_chunk_size);
    }
    return 0;
}

static const RtmpChunkStream::MessageHandler s_msg_handlers[] = {
    &RtmpChunkStream::OnSetChunkSize, // 1
    &RtmpChunkStream::OnAbortMessage, // 2
    &RtmpChunkStream::OnAck,          // 3
    &RtmpChunkStream::OnUserControlMessage, // 4
    &RtmpChunkStream::OnWindowAckSize,// 5
    &RtmpChunkStream::OnSetPeerBandwidth, // 6
    NULL, //7
    &RtmpChunkStream::OnAudioMessage, // 8
    &RtmpChunkStream::OnVideoMessage, // 9
    NULL, // 10
    NULL, // 11
    NULL, // 12
    NULL, // 13
    NULL, // 14
    &RtmpChunkStream::OnDataMessageAMF3, // 15
    &RtmpChunkStream::OnSharedObjectMessageAMF3, // 16
    &RtmpChunkStream::OnCommandMessageAMF3, // 17
    &RtmpChunkStream::OnDataMessageAMF0, // 18
    &RtmpChunkStream::OnSharedObjectMessageAMF0, // 19
    &RtmpChunkStream::OnCommandMessageAMF0, // 20
    NULL, // 21
    &RtmpChunkStream::OnAggregateMessage, // 22
};

typedef butil::FlatMap<std::string, RtmpChunkStream::CommandHandler> CommandHandlerMap;
static CommandHandlerMap* s_cmd_handlers = NULL;
static pthread_once_t s_cmd_handlers_init_once = PTHREAD_ONCE_INIT;
static void InitCommandHandlers() {
    // Dispatch commands based on "Command Name".
    s_cmd_handlers = new CommandHandlerMap;
    CHECK_EQ(0, s_cmd_handlers->init(64, 70));
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_CONNECT] = &RtmpChunkStream::OnConnect;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_ON_BW_DONE] = &RtmpChunkStream::OnBWDone;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_RESULT] = &RtmpChunkStream::OnResult;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_ERROR] = &RtmpChunkStream::OnError;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_PLAY] = &RtmpChunkStream::OnPlay;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_PLAY2] = &RtmpChunkStream::OnPlay2;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_CREATE_STREAM] = &RtmpChunkStream::OnCreateStream;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_DELETE_STREAM] = &RtmpChunkStream::OnDeleteStream;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_CLOSE_STREAM] = &RtmpChunkStream::OnCloseStream;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_PUBLISH] = &RtmpChunkStream::OnPublish;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_SEEK] = &RtmpChunkStream::OnSeek;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_PAUSE] = &RtmpChunkStream::OnPause;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_ON_STATUS] = &RtmpChunkStream::OnStatus;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_RELEASE_STREAM] = &RtmpChunkStream::OnReleaseStream;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_FC_PUBLISH] = &RtmpChunkStream::OnFCPublish;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_FC_UNPUBLISH] = &RtmpChunkStream::OnFCUnpublish;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_GET_STREAM_LENGTH] = &RtmpChunkStream::OnGetStreamLength;
    (*s_cmd_handlers)[RTMP_AMF0_COMMAND_CHECK_BW] = &RtmpChunkStream::OnCheckBW;
}

bool RtmpChunkStream::OnMessage(const RtmpBasicHeader& bh,
                                const RtmpMessageHeader& mh,
                                butil::IOBuf* msg_body,
                                Socket* socket) {
    // Make sure msg_body is consistent with the header. Previous code
    // forgot to clear msg_body before appending new message.
    CHECK_EQ((size_t)mh.message_length, msg_body->size());
    
    if (mh.message_type >= 1 && mh.message_type <= 6) {
        // protocol/user control messages MUST/SHOULD have message stream ID 0
        // (known as the control stream) and be sent in chunk stream ID 2.
        // control messages take effect as soon as they are received; their
        // timestamps are ignored.
        if (mh.stream_id != 0 || bh.chunk_stream_id != 2) {
            RTMP_ERROR(socket, mh) << "Control messages should be sent on "
                "stream_id=0 chunk_stream_id=2";
        }
    }
    const uint32_t index = mh.message_type - 1;
    if (index >= arraysize(s_msg_handlers)) {
        RTMP_ERROR(socket, mh) << "Unknown message_type=" << (int)mh.message_type;
        return false;
    }
    MessageHandler handler = s_msg_handlers[index];
    if (handler == NULL) {
        RTMP_ERROR(socket, mh) << "Unknown message_type=" << (int)mh.message_type;
        return false;
    }
    // audio/video/ack are more verbose than other messages.
    const int vlvl = ((mh.message_type != RTMP_MESSAGE_AUDIO &&
                                mh.message_type != RTMP_MESSAGE_VIDEO &&
                                mh.message_type != RTMP_MESSAGE_ACK) ?
                               (RPC_VLOG_LEVEL + 1) : (RPC_VLOG_LEVEL + 2));
    VLOG(vlvl) << socket->remote_side() << "[" << mh.stream_id
                        << "] Message{timestamp=" << mh.timestamp
                        << " type=" << messagetype2str(mh.message_type)
                        << " body_size=" << mh.message_length << '}';
    return (this->*handler)(mh, msg_body, socket);
}

bool RtmpChunkStream::OnSetChunkSize(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length != 4u) {
        RTMP_ERROR(socket, mh) << "Expected message_length=4, actually "
                               << mh.message_length;
        return false;
    }
    char buf[4];
    msg_body->cutn(buf, sizeof(buf));
    const uint32_t new_size = ReadBigEndian4Bytes(buf);
    if (new_size > 0x7FFFFFFF) {
        RTMP_ERROR(socket, mh) << "MSB of chunk_size=" << new_size
                               << " is not zero";
        return false;
    }
    const uint32_t old_size = connection_context()->_chunk_size_in;
    connection_context()->_chunk_size_in = new_size;
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] SetChunkSize: " << old_size << " -> " << new_size;
    return true;
}

bool RtmpChunkStream::OnAbortMessage(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length != 4u) {
        RTMP_ERROR(socket, mh) << "Expected message_length=4, actually "
                               << mh.message_length;
        return false;
    }
    char buf[4];
    msg_body->cutn(buf, sizeof(buf));
    uint32_t cs_id = ReadBigEndian4Bytes(buf);
    if (cs_id > RTMP_MAX_CHUNK_STREAM_ID) {
        RTMP_ERROR(socket, mh) << "Invalid chunk_stream_id=" << cs_id;
        return false;
    }
    connection_context()->ClearChunkStream(cs_id);
    return true;
}

bool RtmpChunkStream::OnAck(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length != 4u) {
        RTMP_ERROR(socket, mh) << "Expected message_length=4, actually "
                               << mh.message_length;
        return false;
    }
    char buf[4];
    msg_body->cutn(buf, sizeof(buf));
    uint32_t bytes_received = ReadBigEndian4Bytes(buf);
    // TODO(gejun): No usage right now.
    (void)bytes_received;
    //RPC_VLOG << socket->remote_side() << ": Ack=" << bytes_received;
    return true;
}

bool RtmpChunkStream::OnWindowAckSize(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length != 4u) {
        RTMP_ERROR(socket, mh) << "Expected message_length=4, actually "
                               << mh.message_length;
        return false;
    }
    char buf[4];
    msg_body->cutn(buf, sizeof(buf));
    const uint32_t old_size = connection_context()->_window_ack_size;
    const uint32_t new_size = ReadBigEndian4Bytes(buf);
    connection_context()->_window_ack_size = new_size;
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] WindowAckSize: " << old_size << " -> " << new_size;
    return true;
}

bool RtmpChunkStream::OnSetPeerBandwidth(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length != 5u) {
        RTMP_ERROR(socket, mh) << "Expected message_length=5, actually "
                               << mh.message_length;
        return false;
    }
    char buf[5];
    msg_body->cutn(buf, sizeof(buf));
    const uint32_t new_size = ReadBigEndian4Bytes(buf);
    const RtmpLimitType limit_type = (RtmpLimitType)buf[4];
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] SetPeerBandwidth=" << new_size
             << " limit_type=" << limit_type;
    return true;
}

bool RtmpChunkStream::OnUserControlMessage(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    if (mh.message_length > 32) {
        RTMP_ERROR(socket, mh) << "No user control message long as "
                               << mh.message_length << " bytes";
        return false;
    }
    char buf[mh.message_length]; // safe to put on stack.
    msg_body->cutn(buf, mh.message_length);
    const uint16_t event_type = ReadBigEndian2Bytes(buf);
    butil::StringPiece event_data(buf + 2, mh.message_length - 2);
    switch ((RtmpUserControlEventType)event_type) {
    case RTMP_USER_CONTROL_EVENT_STREAM_BEGIN:
        return OnStreamBegin(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_STREAM_EOF:
        return OnStreamEOF(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_STREAM_DRY:
        return OnStreamDry(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_SET_BUFFER_LENGTH:
        return OnSetBufferLength(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_STREAM_IS_RECORDED:
        return OnStreamIsRecorded(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_PING_REQUEST:
        return OnPingRequest(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_PING_RESPONSE:
        return OnPingResponse(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_BUFFER_EMPTY:
        return OnBufferEmpty(mh, event_data, socket);
    case RTMP_USER_CONTROL_EVENT_BUFFER_READY:
        return OnBufferReady(mh, event_data, socket);
    } // switch(event_type)
    RTMP_ERROR(socket, mh) << "Unknown event_type=" << event_type;
    return false;
}

bool RtmpChunkStream::OnStreamBegin(const RtmpMessageHeader& mh,
                                    const butil::StringPiece& event_data,
                                    Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service != NULL) {
        RTMP_ERROR(socket, mh) << "Server should not receive `StreamBegin'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid StreamBegin.event_data.size="
                               << event_data.size();
        return false;
    }
    // Ignore StreamBegin
    return true;
}

bool RtmpChunkStream::OnStreamEOF(const RtmpMessageHeader& mh,
                                  const butil::StringPiece& event_data,
                                  Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service != NULL) {
        RTMP_ERROR(socket, mh) << "Server should not receive `StreamEOF'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid StreamEOF.event_data.size="
                               << event_data.size();
        return false;
    }
    // Ignore StreamEOF
    return true;
}

bool RtmpChunkStream::OnStreamDry(const RtmpMessageHeader& mh,
                                  const butil::StringPiece& event_data,
                                  Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service != NULL) {
        RTMP_ERROR(socket, mh) << "Server should not receive `StreamDry'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid StreamDry.event_data.size="
                               << event_data.size();
        return false;
    }
    // Ignore StreamDry
    return true;
}

bool RtmpChunkStream::OnStreamIsRecorded(const RtmpMessageHeader& mh,
                                         const butil::StringPiece& event_data,
                                         Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service != NULL) {
        RTMP_ERROR(socket, mh) << "Server should not receive `StreamIsRecorded'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid StreamIsRecorded.event_data.size="
                               << event_data.size();
        return false;
    }
    // Ignore StreamIsRecorded
    return true;
}

bool RtmpChunkStream::OnSetBufferLength(const RtmpMessageHeader& mh,
                                        const butil::StringPiece& event_data,
                                        Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service == NULL) {
        RTMP_ERROR(socket, mh) << "Client should not receive `SetBufferLength'";
        return false;
    }
    if (event_data.size() != 8) {
        RTMP_ERROR(socket, mh) << "Invalid SetBufferLength.event_data.size="
                               << event_data.size();
        return false;
    }
    const uint32_t stream_id = ReadBigEndian4Bytes(event_data.data());
    const uint32_t bl_ms = ReadBigEndian4Bytes(event_data.data() + 4);
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] SetBufferLength{stream_id=" << stream_id
             << " buffer_length_ms=" << bl_ms << '}';
    if (stream_id == RTMP_CONTROL_MESSAGE_STREAM_ID) {
        // Ignore SetBufferSize for control stream.
        return true;
    }
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(stream_id, &stream)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << stream_id;
        return false;
    }
    static_cast<RtmpServerStream*>(stream.get())->OnSetBufferLength(bl_ms);
    return true;
}

bool RtmpChunkStream::OnPingRequest(const RtmpMessageHeader& mh,
                                    const butil::StringPiece& event_data,
                                    Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service != NULL) {
        RTMP_ERROR(socket, mh) << "Server should not receive `PingRequest'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid PingRequest.event_data.size="
                               << event_data.size();
        return false;
    }
    const uint32_t timestamp = ReadBigEndian4Bytes(event_data.data());
    char data[6];
    char* p = data;
    WriteBigEndian2Bytes(&p, RTMP_USER_CONTROL_EVENT_PING_RESPONSE);
    WriteBigEndian4Bytes(&p, timestamp);
    SocketMessagePtr<RtmpUnsentMessage> msg(
        MakeUnsentControlMessage(RTMP_MESSAGE_USER_CONTROL, data, sizeof(data)));
    if (socket->Write(msg) != 0) {
        PLOG(WARNING) << "Fail to send back PingResponse";
        return false;
    }
    return true;
}

bool RtmpChunkStream::OnPingResponse(const RtmpMessageHeader& mh,
                                     const butil::StringPiece& event_data,
                                     Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service == NULL) {
        RTMP_ERROR(socket, mh) << "Client should not receive `PingResponse'";
        return false;
    }
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid PingResponse.event_data.size="
                               << event_data.size();
        return false;
    }
    const uint32_t timestamp = ReadBigEndian4Bytes(event_data.data());
    service->OnPingResponse(socket->remote_side(), timestamp);
    return true;
}

bool RtmpChunkStream::OnBufferEmpty(const RtmpMessageHeader& mh,
                                    const butil::StringPiece& event_data,
                                    Socket* socket) {
    // Ignore right now.
    // NOTE: If we need to fetch the data from FMS as fast as possible,
    // we may consider the pause/unpause trick used in http://repo.or.cz/w/rtmpdump.git/blob/8880d1456b282ee79979adbe7b6a6eb8ad371081:/librtmp/rtmp.c#l2787
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid BufferEmpty.event_data.size="
                               << event_data.size();
        return false;
    }
    const uint32_t tmp = ReadBigEndian4Bytes(event_data.data());
    const int vlvl = RPC_VLOG_LEVEL + 1;
    VLOG(vlvl) << socket->remote_side() << "[" << mh.stream_id
                        << "] BufferEmpty(" << tmp << ')';
    return true;
}

bool RtmpChunkStream::OnBufferReady(const RtmpMessageHeader& mh,
                                    const butil::StringPiece& event_data,
                                    Socket* socket) {
    if (event_data.size() != 4) {
        RTMP_ERROR(socket, mh) << "Invalid BufferReady.event_data.size="
                               << event_data.size();
        return false;
    }
    const uint32_t tmp = ReadBigEndian4Bytes(event_data.data());
    const int vlvl = RPC_VLOG_LEVEL + 1;
    VLOG(vlvl) << socket->remote_side() << "[" << mh.stream_id
                        << "] BufferReady(" << tmp << ')';
    return true;
}

bool RtmpChunkStream::OnAudioMessage(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    char first_byte = 0;
    if (!msg_body->cut1(&first_byte)) {
        // pretty common, don't print logs.
        return false;
    }
    // TODO: execq?
    RtmpAudioMessage msg;
    msg.timestamp = mh.timestamp;
    msg.codec = (FlvAudioCodec)((first_byte >> 4) & 0xF);
    msg.rate = (FlvSoundRate)((first_byte >> 2) & 0x3);
    msg.bits = (FlvSoundBits)((first_byte >> 1) & 0x1);
    msg.type = (FlvSoundType)(first_byte & 0x1);
    msg_body->swap(msg.data);

    const int vlvl = RPC_VLOG_LEVEL + 1;
    VLOG(vlvl) << socket->remote_side() << "[" << mh.stream_id << "] " << msg;
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        LOG_EVERY_SECOND(WARNING) << socket->remote_side()
                                  << ": Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    stream->CallOnAudioMessage(&msg);
    return true;
}

bool RtmpChunkStream::OnVideoMessage(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    char first_byte = 0;
    if (!msg_body->cut1(&first_byte)) {
        // pretty common, don't print logs.
        return false;
    }
    // TODO: execq?
    RtmpVideoMessage msg;
    msg.timestamp = mh.timestamp;
    msg.frame_type = (FlvVideoFrameType)((first_byte >> 4) & 0xF);
    msg.codec = (FlvVideoCodec)(first_byte & 0xF);
    if (!is_video_frame_type_valid(msg.frame_type)) {
        RTMP_WARNING(socket, mh) << "Invalid frame_type=" << (int)msg.frame_type;
    }
    if (!is_video_codec_valid(msg.codec)) {
        RTMP_WARNING(socket, mh) << "Invalid codec=" << (int)msg.codec;
    }
    msg_body->swap(msg.data);

    const int vlvl = RPC_VLOG_LEVEL + 1;
    VLOG(vlvl) << socket->remote_side() << "[" << mh.stream_id << "] " << msg;
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        LOG_EVERY_SECOND(WARNING) << socket->remote_side()
                                  << ": Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    stream->CallOnVideoMessage(&msg);
    return true;
}

bool RtmpChunkStream::OnDataMessageAMF0(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    butil::IOBufAsZeroCopyInputStream zc_stream(*msg_body);
    AMFInputStream istream(&zc_stream);
    std::string name;
    if (!ReadAMFString(&name, &istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read name of DataMessage";
        return false;
    }
    if (name == RTMP_AMF0_SET_DATAFRAME) {
        if (!ReadAMFString(&name, &istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read name of DataMessage";
            return false;
        }
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] DataMessage{timestamp=" << mh.timestamp
             << " name=" << name << '}';

    if (name == RTMP_AMF0_ON_META_DATA || name == FLAGS_user_defined_data_message) {
        if (istream.check_emptiness()) {
            // Ignore empty metadata (seen in pulling streams from quanmin)
            return false;
        }
        RtmpMetaData metadata;
        metadata.timestamp = mh.timestamp;
        if (!ReadAMFObject(&metadata.data, &istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read metadata";
            return false;
        }
        // TODO: execq?
        butil::intrusive_ptr<RtmpStreamBase> stream;
        if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
            LOG_EVERY_SECOND(WARNING) << socket->remote_side()
                                      << ": Fail to find stream_id=" << mh.stream_id;
            return false;
        }
        stream->CallOnMetaData(&metadata, name);
        return true;
    } else if (name == RTMP_AMF0_ON_CUE_POINT) {
        if (istream.check_emptiness()) {
            return false;
        }
        RtmpCuePoint cuepoint;
        cuepoint.timestamp = mh.timestamp;
        if (!ReadAMFObject(&cuepoint.data, &istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read cuepoint";
            return false;
        }
        // TODO: execq?
        butil::intrusive_ptr<RtmpStreamBase> stream;
        if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
            LOG_EVERY_SECOND(WARNING) << socket->remote_side()
                                      << ": Fail to find stream_id=" << mh.stream_id;
            return false;
        }
        stream->CallOnCuePoint(&cuepoint);
        return true;
    } else if (name == RTMP_AMF0_DATA_SAMPLE_ACCESS) {
        return true;
    } else if (name == RTMP_AMF0_COMMAND_ON_STATUS) {
        return true;
    }
    return false;
}

bool RtmpChunkStream::OnSharedObjectMessageAMF0(
    const RtmpMessageHeader&, butil::IOBuf*, Socket* socket) {
    LOG_EVERY_SECOND(ERROR) << socket->remote_side() << ": Not implemented";
    return false;
}

bool RtmpChunkStream::OnCommandMessageAMF0(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    butil::IOBufAsZeroCopyInputStream zc_stream(*msg_body);
    AMFInputStream istream(&zc_stream);
    std::string command_name;
    if (!ReadAMFString(&command_name, &istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read commandName";
        return false;
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] Command{timestamp=" << mh.timestamp
             << " name=" << command_name << '}';
    pthread_once(&s_cmd_handlers_init_once, InitCommandHandlers);
    RtmpChunkStream::CommandHandler* phandler =
        s_cmd_handlers->seek(command_name);
    if (phandler == NULL) {
        RTMP_ERROR(socket, mh) << "Unknown command_name=" << command_name;
        return false;
    }
    return (this->**phandler)(mh, &istream, socket);
}

bool RtmpChunkStream::OnDataMessageAMF3(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    msg_body->pop_front(1);
    return OnDataMessageAMF0(mh, msg_body, socket);
}

bool RtmpChunkStream::OnSharedObjectMessageAMF3(
    const RtmpMessageHeader&, butil::IOBuf*, Socket*) {
    LOG(ERROR) << "Not implemented";
    return false;
}

bool RtmpChunkStream::OnCommandMessageAMF3(
    const RtmpMessageHeader& mh, butil::IOBuf* msg_body, Socket* socket) {
    msg_body->pop_front(1);
    return OnCommandMessageAMF0(mh, msg_body, socket);
}

bool RtmpChunkStream::OnAggregateMessage(
    const RtmpMessageHeader&, butil::IOBuf*, Socket*) {
    LOG(ERROR) << "Not implemented";
    return false;
}

// // Setup necessary fields in the controller.
// static void SetupRtmpServerController(Controller* cntl,
//                                       const Server* server,
//                                       const Socket* socket) {
//     ControllerPrivateAccessor accessor(cntl);
//     ServerPrivateAccessor server_accessor(server);
//     const bool security_mode = server->options().security_mode() &&
//         socket->user() == server_accessor.acceptor();
//     accessor.set_server(server)
//         .set_security_mode(security_mode)
//         .set_peer_id(socket->id())
//         .set_remote_side(socket->remote_side())
//         .set_local_side(socket->local_side())
//         .set_auth_context(socket->auth_context())
//         .set_request_protocol(PROTOCOL_RTMP);
// }

bool RtmpChunkStream::OnConnect(const RtmpMessageHeader& mh,
                                AMFInputStream* istream,
                                Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `connect'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read connect.TransactionId";
        return false;
    }
    RtmpConnectRequest* req = &connection_context()->_connect_req;
    if (!ReadAMFObject(req, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read connect.CommandObjects";
        return false;
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] connect{" << req->ShortDebugString() << '}';

    TemporaryArrayBuilder<SocketMessagePtr<RtmpUnsentMessage>, 5> msgs;
    char* p = NULL;
    // WindowAckSize
    // TODO(gejun): seems not effective to ffplay.
    char wasbuf[4];
    p = wasbuf;
    WriteBigEndian4Bytes(&p, FLAGS_rtmp_server_window_ack_size);
    msgs.push().reset(MakeUnsentControlMessage(
                          RTMP_MESSAGE_WINDOW_ACK_SIZE, wasbuf, sizeof(wasbuf)));
    
    // SetPeerBandwidth
    char spbbuf[5];
    p = spbbuf;
    WriteBigEndian4Bytes(&p, FLAGS_rtmp_server_window_ack_size);
    *p++ = RTMP_LIMIT_DYNAMIC;
    msgs.push().reset(MakeUnsentControlMessage(
                          RTMP_MESSAGE_SET_PEER_BANDWIDTH, spbbuf, sizeof(spbbuf)));

    // SetChunkSize.
    // @SRS
    // set chunk size to larger.
    // set the chunk size before any larger response greater than 128,
    // to make OBS happy, @see https://github.com/ossrs/srs/issues/454
    // NOTE(gejun): This also makes ffmpeg/ffplay *much* faster for publishing
    // and playing http://code.bj.bcebos.com/bbb_1080p.flv at 2k bitrate.
    char csbuf[4];
    p = csbuf;
    WriteBigEndian4Bytes(&p, FLAGS_rtmp_server_chunk_size);
    RtmpUnsentMessage* scs_msg = MakeUnsentControlMessage(
        RTMP_MESSAGE_SET_CHUNK_SIZE, csbuf, sizeof(csbuf));
    scs_msg->new_chunk_size = FLAGS_rtmp_server_chunk_size;
    msgs.push().reset(scs_msg);
    
    // _result
    butil::IOBuf req_buf;
    RtmpInfo info;
    RtmpConnectResponse response;
    // TODO: Set this field.
    std::string error_text; 
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString((!error_text.empty() ? RTMP_AMF0_COMMAND_ERROR :
                        RTMP_AMF0_COMMAND_RESULT), &ostream);
        WriteAMFUint32(1, &ostream);
        if (!response.has_fmsver()) {
            response.set_fmsver("FMS/" RTMP_SIG_FMS_VER);
        }
        if (!response.has_capabilities()) {
            response.set_capabilities(127);
        }
        if (!response.has_mode()) {
            response.set_mode(1);
        }
        response.set_create_stream_with_play_or_publish(true);
        WriteAMFObject(response, &ostream);
        // Set info
        if (error_text.empty()) {
            info.set_code(RTMP_STATUS_CODE_CONNECT_SUCCESS);
            info.set_level(RTMP_INFO_LEVEL_STATUS);
            info.set_description("Connection succeeded");
            info.set_objectencoding(req->objectencoding());
        } else {
            info.set_code(RTMP_STATUS_CODE_CONNECT_REJECTED);
            info.set_level(RTMP_INFO_LEVEL_ERROR);
            info.set_description(error_text);
        }
        WriteAMFObject(info, &ostream);
        CHECK(ostream.good());
    }
    msgs.push().reset(MakeUnsentControlMessage(
                          RTMP_MESSAGE_COMMAND_AMF0, chunk_stream_id(), req_buf));

    // onBWDone
    req_buf.clear();
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_ON_BW_DONE, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        CHECK(ostream.good());
    }
    // chunk_stream_id is same with _result to connect, confirmed in SRS.
    msgs.push().reset(MakeUnsentControlMessage(
                          RTMP_MESSAGE_COMMAND_AMF0, chunk_stream_id(), req_buf));

    for (size_t i = msgs.size(); i > 1; --i) {
        msgs[i-2]->next.reset(msgs[i-1].release());
    }
    if (socket->Write(msgs[0]) != 0) {
        PLOG(WARNING) << socket->remote_side() << ": Fail to respond connect";
        socket->SetFailed(EFAILEDSOCKET, "Fail to respond connect");
        return false;
    }
    RPC_VLOG << socket->remote_side() << ": respond connect, props={"
             << response.ShortDebugString()
             << "} info={" << info.ShortDebugString() << '}';
    return true;
}

bool RtmpChunkStream::OnBWDone(const RtmpMessageHeader& mh,
                               AMFInputStream*,
                               Socket* socket) {
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] ignore onBWDone";
    return true;
}

void RtmpContext::OnConnected(int error_code) {
    if (_on_connect != NULL) {
        void (*saved_on_connect)(int, void*) = _on_connect;
        void* saved_arg = _on_connect_arg;
        _on_connect = NULL;
        saved_on_connect(error_code, saved_arg);
    }
}

bool RtmpChunkStream::OnResult(const RtmpMessageHeader& mh,
                               AMFInputStream* istream, Socket* socket) {
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read _result.TransactionId";
        return false;
    }
    if (transaction_id < 2) {
        if (transaction_id == 1) {
            RtmpConnectResponse connect_res;
            if (!ReadAMFObject(&connect_res, istream)) {
                RTMP_ERROR(socket, mh) << "Fail to read _result.Properties";
                return false;
            }
            if (!_conn_ctx->_simplified_rtmp) {
                // In simplified rtmp case, connection_context()->_create_stream_with_play_or_publish 
                // is set and OnConnected is called in RtmpConnect::StartConnect, so we don't need
                // to do these operations again here.
                if (connect_res.create_stream_with_play_or_publish()) {
                    connection_context()->_create_stream_with_play_or_publish = true;
                }
                connection_context()->OnConnected(0);
            } else {
                CHECK(connect_res.create_stream_with_play_or_publish());
            }
        } // else nothing to do with transaction_id=0
        return true;
    }
    if (connection_context()->unconnected()) {
        RTMP_ERROR(socket, mh) << "Received _result.TransactionId="
                               << transaction_id << " before connected";
    }
    RtmpContext* ctx = static_cast<RtmpContext*>(socket->parsing_context());
    RtmpTransactionHandler* handler = ctx->RemoveTransaction(transaction_id);
    if (handler == NULL) {
        RTMP_WARNING(socket, mh) << "Unknown _result.TransactionId="
                                 << transaction_id;
        return false;
    }
    // TODO: Should Run return bool?
    handler->Run(false, mh, istream, socket);
    return true;
}

bool RtmpChunkStream::OnError(const RtmpMessageHeader& mh,
                              AMFInputStream* istream,
                              Socket* socket) {
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read _error.TransactionId";
        return false;
    }
    if (transaction_id < 2) {
        if (transaction_id == 1) {
            connection_context()->OnConnected(-1);
        } // else nothing to do with transaction_id=0
        return true;
    }
    if (connection_context()->unconnected()) {
        RTMP_ERROR(socket, mh) << "Received _error.TransactionId="
                               << transaction_id << " before connected";
    }
    RtmpContext* ctx = static_cast<RtmpContext*>(socket->parsing_context());
    RtmpTransactionHandler* handler = ctx->RemoveTransaction(transaction_id);
    if (handler == NULL) {
        RTMP_WARNING(socket, mh) << "Unknown _error.TransactionId="
                                 << transaction_id;
        return false;
    }
    handler->Run(true, mh, istream, socket);
    return true;
}

bool RtmpChunkStream::OnStatus(const RtmpMessageHeader& mh,
                               AMFInputStream* istream,
                               Socket* socket) {
    if (!connection_context()->is_client_side()) {
        RTMP_ERROR(socket, mh) << "Server-side should not receive `onStatus'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read onStatus.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read onStatus.CommandObject";
        return false;
    }
    RtmpInfo info;
    if (!ReadAMFObject(&info, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read onStatus.InfoObject";
        return false;
    }
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] onStatus{" << info.ShortDebugString() << '}';
    static_cast<RtmpClientStream*>(stream.get())->OnStatus(info);
    return true;
}

bool RtmpChunkStream::OnCreateStream(const RtmpMessageHeader& mh,
                                     AMFInputStream* istream,
                                     Socket* socket) {
    RtmpService* service = connection_context()->service();
    if (service == NULL) {
        RTMP_ERROR(socket, mh) << "Client should not receive `createStream'";
        return false;
    }
    double transaction_id = 0;
    if (!ReadAMFNumber(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read createStream.TransactionId";
        return false;
    }
    bool is_publish = false;
    RtmpPublishType publish_type = RTMP_PUBLISH_LIVE;
    std::string stream_name;
    AMFObject cmd_obj;
    if (!ReadAMFObject(&cmd_obj, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read createStream.CommandObject";
        return false;
    }
    const AMFField* cmd_name_field = cmd_obj.Find("CommandName");
    if (cmd_name_field != NULL && cmd_name_field->IsString()) {
        is_publish = (cmd_name_field->AsString() == "publish");
    }
    const AMFField* stream_name_field = cmd_obj.Find("StreamName");
    if (stream_name_field != NULL && stream_name_field->IsString()) {
        stream_name_field->AsString().CopyToString(&stream_name);
    }
    if (is_publish) {
        const AMFField* publish_type_field = cmd_obj.Find("PublishType");
        if (publish_type_field != NULL && publish_type_field->IsString()) {
            Str2RtmpPublishType(publish_type_field->AsString(), &publish_type);
        }
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] createStream{transaction_id=" << transaction_id << '}';
    std::string error_text;
    butil::intrusive_ptr<RtmpServerStream> stream(
        service->NewStream(connection_context()->_connect_req));
    if (connection_context()->_connect_req.stream_multiplexing() &&
        stream != NULL) {
        stream->_client_supports_stream_multiplexing = true;
    }
    if (NULL == stream) {
        error_text = "Fail to create stream";
        LOG(ERROR) << error_text;
    } else {
        socket->ReAddress(&stream->_rtmpsock);
        if (!connection_context()->AddServerStream(stream.get())) {
            error_text = "Fail to add stream";
            LOG(ERROR) << error_text;
        } else {
            const int rc = bthread_id_create(&stream->_onfail_id, stream.get(),
                                             RtmpServerStream::RunOnFailed);
            if (rc) {
                LOG(ERROR) << "Fail to create RtmpServerStream._onfail_id: "
                           << berror(rc);
                stream->OnStopInternal();
                return false;
            }
            // Add a ref for RunOnFailed.
            butil::intrusive_ptr<RtmpServerStream>(stream).detach();
            socket->fail_me_at_server_stop();
            socket->NotifyOnFailed(stream->_onfail_id);
        }
    }
    // Respond createStream
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString((!error_text.empty() ? RTMP_AMF0_COMMAND_ERROR :
                        RTMP_AMF0_COMMAND_RESULT), &ostream);
        WriteAMFNumber(transaction_id, &ostream);
        if (error_text.empty()) {
            if (!stream_name.empty()) {
                AMFObject cmd_obj;
                cmd_obj.SetBool("PlayOrPublishAccepted", true);
                WriteAMFObject(cmd_obj, &ostream);
            } else {
                WriteAMFNull(&ostream);
            }
            WriteAMFUint32(stream->stream_id(), &ostream);
        } else {
            WriteAMFNull(&ostream);
            RtmpInfo info;
            info.set_level(RTMP_INFO_LEVEL_ERROR);
             // TODO(gejun): Not sure about the code.
            info.set_code("NetConnection.CreateStream.Rejected");
            info.set_description(error_text);
            WriteAMFObject(info, &ostream);
        }
        CHECK(ostream.good());
    }
    SocketMessagePtr<RtmpUnsentMessage> msg(
        MakeUnsentControlMessage(
            RTMP_MESSAGE_COMMAND_AMF0, chunk_stream_id(), req_buf));
    if (WriteWithoutOvercrowded(socket, msg) != 0) {
        PLOG(WARNING) << socket->remote_side() << '[' << mh.stream_id
                      << "] Fail to respond createStream";
        // End the stream at server-side.
        const bthread_id_t id = stream->_onfail_id;
        if (id != INVALID_BTHREAD_ID) {
            bthread_id_error(id, 0);
        }
        return false;
    }
    if (!error_text.empty()) {
        return false;
    }
    if (stream_name.empty()) {
        return true;
    }
    butil::IOBuf cmd_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_ostream(&cmd_buf);
        AMFOutputStream ostream(&zc_ostream);
        WriteAMFUint32(0, &ostream);  // TransactionId
        WriteAMFNull(&ostream);       // CommandObject
        WriteAMFString(stream_name, &ostream); // StreamName
        if (is_publish) {
            WriteAMFString(RtmpPublishType2Str(publish_type), &ostream);
        }
    }
    butil::IOBufAsZeroCopyInputStream zc_istream(cmd_buf);
    AMFInputStream cmd_istream(&zc_istream);
    RtmpMessageHeader header;
    header.timestamp = mh.timestamp;
    header.message_length = cmd_buf.size();
    header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    header.stream_id = stream->stream_id();
    if (is_publish) {
        return OnPublish(header, &cmd_istream, socket);
    } else {
        return OnPlay(header, &cmd_istream, socket);
    }
}

class OnPlayContinuation : public google::protobuf::Closure {
public:
    void Run();
public:
    butil::Status status;
    butil::intrusive_ptr<RtmpServerStream> player_stream;
};

void OnPlayContinuation::Run() {
    std::unique_ptr<OnPlayContinuation> delete_self(this);

    if (status.ok()) {
        // nothing to do, we already sent NetStream.Play.Reset/Start stuff in
        // RtmpContext::OnPlay(). Notice that we can't send those stuff here
        // because user may write to players inside RtmpStreamBase::OnPlay()
        // (with cached headers/metadata) before calling this Run(), which
        // causes flashplayer to not work(black screen).
        return;
    }

    if (player_stream->SendStopMessage(status.error_cstr()) != 0) {
        PLOG(WARNING) << "Fail to send StreamNotFound to "
                      << player_stream->remote_side();
    }
    if (FLAGS_log_error_text) {
        LOG(WARNING) << "Error to " << player_stream->remote_side() << '['
                     << player_stream->stream_id() << "]: " << status;
    }
}

bool RtmpChunkStream::OnPlay(const RtmpMessageHeader& mh,
                             AMFInputStream* istream,
                             Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `play'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play.TransactionId";
        return false;
    }
    // ffmpeg send non-zero transaction_id for play.
    
    if (!ReadAMFNull(istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play.CommandObject";
        return false;
    }
    RtmpPlayOptions play_opt; // inner fields are initialized with defaults.
    if (!ReadAMFString(&play_opt.stream_name, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play.StreamName";
        return false;
    }
    // start/duration/reset are optional, check emptiness of the stream before
    // calling ReadAMFXXX which prints log for failed branches.
    if (!istream->check_emptiness()) {
        if (!ReadAMFNumber(&play_opt.start, istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read play.Start";
            return false;
        }
    }
    if (!istream->check_emptiness()) {
        if (!ReadAMFNumber(&play_opt.duration, istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read play.Duration";
            return false;
        }
    }
    if (!istream->check_emptiness()) {
        if (!ReadAMFBool(&play_opt.reset, istream)) {
            RTMP_ERROR(socket, mh) << "Fail to read play.Reset";
            return false;
        }
    }
    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] play{transaction_id=" << transaction_id
             << " stream_name=" << play_opt.stream_name
             << " start=" << play_opt.start
             << " duration=" << play_opt.duration
             << " reset=" << play_opt.reset << '}';

    butil::IOBuf req_buf;
    TemporaryArrayBuilder<SocketMessagePtr<RtmpUnsentMessage>, 5> msgs;
    
    // TODO(gejun): RTMP spec sends StreamIsRecorded before StreamBegin
    // however SRS does not.
    // StreamBegin
    {
        char cntl_buf[6];
        char* p = cntl_buf;
        WriteBigEndian2Bytes(&p, RTMP_USER_CONTROL_EVENT_STREAM_BEGIN);
        WriteBigEndian4Bytes(&p, mh.stream_id);
        msgs.push().reset(MakeUnsentControlMessage(
                              RTMP_MESSAGE_USER_CONTROL, cntl_buf, sizeof(cntl_buf)));
    }
    // Play.Reset
    if (play_opt.reset) {
        // According to RTMP spec: NetStream.Play.Reset is sent only if the
        // play command sent by the client has set the reset flag. 
        req_buf.clear();
        {
            butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
            AMFOutputStream ostream(&zc_stream);
            WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
            WriteAMFUint32(0, &ostream);
            WriteAMFNull(&ostream);
            RtmpInfo info;
            info.set_code(RTMP_STATUS_CODE_PLAY_RESET);
            info.set_level(RTMP_INFO_LEVEL_STATUS);
            info.set_description("Reset " + play_opt.stream_name);
            WriteAMFObject(info, &ostream);
        }
        RtmpUnsentMessage* msg = new RtmpUnsentMessage;
        msg->header.message_length = req_buf.size();
        msg->header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
        msg->header.stream_id = mh.stream_id;
        msg->chunk_stream_id = chunk_stream_id();
        msg->body = req_buf;
        msgs.push().reset(msg);
    }
    
    // Play.Start
    req_buf.clear();
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        RtmpInfo info;
        info.set_code(RTMP_STATUS_CODE_PLAY_START);
        info.set_level(RTMP_INFO_LEVEL_STATUS);
        info.set_description("Start playing " + play_opt.stream_name);
        WriteAMFObject(info, &ostream);
    }
    RtmpUnsentMessage* msg2 = new RtmpUnsentMessage;
    msg2->header.message_length = req_buf.size();
    msg2->header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    msg2->header.stream_id = mh.stream_id;
    msg2->chunk_stream_id = chunk_stream_id();
    msg2->body = req_buf;
    msgs.push().reset(msg2);

    // |RtmpSampleAccess(true, true)
    req_buf.clear();
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_SAMPLE_ACCESS, &ostream);
        WriteAMFBool(true, &ostream);
        WriteAMFBool(true, &ostream);
    }
    RtmpUnsentMessage* msg3 = new RtmpUnsentMessage;
    msg3->header.message_length = req_buf.size();
    msg3->header.message_type = RTMP_MESSAGE_DATA_AMF0;
    msg3->header.stream_id = mh.stream_id;
    msg3->chunk_stream_id = chunk_stream_id();
    msg3->body = req_buf;
    msgs.push().reset(msg3);

    // onStatus(NetStream.Data.Start)
    req_buf.clear();
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
        RtmpInfo info;
        info.set_code(RTMP_STATUS_CODE_DATA_START);
        WriteAMFObject(info, &ostream);
    }
    RtmpUnsentMessage* msg4 = new RtmpUnsentMessage;
    msg4->header.message_length = req_buf.size();
    msg4->header.message_type = RTMP_MESSAGE_DATA_AMF0;
    msg4->header.stream_id = mh.stream_id;
    msg4->chunk_stream_id = chunk_stream_id();
    msg4->body = req_buf;
    msgs.push().reset(msg4);

    butil::intrusive_ptr<RtmpStreamBase> stream_guard;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream_guard)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    // Change the chunk_stream_id of the server stream to be same with play,
    // so that laterly user can call SendXXXMessage successfully.
    stream_guard->_chunk_stream_id = chunk_stream_id();
    RtmpServerStream* stream = static_cast<RtmpServerStream*>(stream_guard.get());

    for (size_t i = msgs.size(); i > 1; --i) {
        msgs[i-2]->next.reset(msgs[i-1].release());
    }
    if (WriteWithoutOvercrowded(socket, msgs[0]) != 0) {
        PLOG(WARNING) << socket->remote_side() << '[' << mh.stream_id
                      << "] Fail to respond play";
        return false;
    }
    // cyberplayer sends play instead of unpause (and send closeStream instead
    // of pause). play automatically unpauses
    if (stream->_paused) {
        stream->_paused = false;
        RPC_VLOG << "Trigger unpause";
        stream->OnPause(false, 0);
    }
    // Call user's callback.
    OnPlayContinuation* done = new OnPlayContinuation;
    done->player_stream.reset(stream, false/*don't add ref*/);
    stream_guard.detach();
    done->player_stream->OnPlay(play_opt, &done->status, done);
    return true;
}

bool RtmpChunkStream::OnPlay2(const RtmpMessageHeader& mh,
                              AMFInputStream* istream,
                              Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `play2'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play2.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play2.CommandObject";
        return false;
    }
    RtmpPlay2Options play2_options;
    if (!ReadAMFObject(&play2_options, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read play2.Parameters";
        return false;
    }
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    static_cast<RtmpServerStream*>(stream.get())->OnPlay2(play2_options);
    return true;
}

bool RtmpChunkStream::OnDeleteStream(const RtmpMessageHeader& mh,
                                     AMFInputStream* istream,
                                     Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `deleteStream'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read deleteStream.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read deleteStream.CommandObject";
        return false;
    }
    uint32_t stream_id = 0;
    if (!ReadAMFUint32(&stream_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read deleteStream.StreamId";
        return false;
    }
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(stream_id, &stream)) {
        // TODO: frequent, commented now
        //RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << stream_id;
        return false;
    }
    bthread_id_t id = static_cast<RtmpServerStream*>(stream.get())->_onfail_id;
    if (id != INVALID_BTHREAD_ID) {
        bthread_id_error(id, 0);
    }
    return true;
}

bool RtmpChunkStream::OnCloseStream(const RtmpMessageHeader& mh,
                                    AMFInputStream* istream,
                                    Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `closeStream'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read closeStream.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read closeStream.CommandObject";
        return false;
    }
    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        // TODO: frequent, commented now
        //RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    if (!stream->_paused) {
        stream->_paused = true;
        // TODO(gejun): Run in execq.
        static_cast<RtmpServerStream*>(stream.get())->OnPause(true, 0);
    }
    return true;
}

class OnPublishContinuation : public google::protobuf::Closure {
public:
    void Run();
public:
    butil::Status status;
    std::string publish_name;
    butil::intrusive_ptr<RtmpServerStream> publish_stream;
};

void OnPublishContinuation::Run() {
    std::unique_ptr<OnPublishContinuation> delete_self(this);
    if (!status.ok()) {
        if (publish_stream->SendStopMessage(status.error_cstr()) != 0) {
            PLOG(WARNING) << "Fail to send StreamNotFound to "
                          << publish_stream->remote_side();
        }
        if (FLAGS_log_error_text) {
            LOG(WARNING) << "Error to " << publish_stream->remote_side()
                         << '[' << publish_stream->stream_id() << "]: "
                         << status;
        }
        return;
    }
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
        WriteAMFUint32(0, &ostream);
        WriteAMFNull(&ostream);
        RtmpInfo info;
        info.set_code(RTMP_STATUS_CODE_PUBLISH_START);
        info.set_level(RTMP_INFO_LEVEL_STATUS);
        info.set_description("Started publishing " + publish_name);
        WriteAMFObject(info, &ostream);
        CHECK(ostream.good());
    }
    SocketMessagePtr<RtmpUnsentMessage> msg(new RtmpUnsentMessage);
    msg->header.message_length = req_buf.size();
    msg->header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    msg->header.stream_id = publish_stream->stream_id();
    msg->chunk_stream_id = publish_stream->chunk_stream_id();
    msg->body = req_buf;

    if (WriteWithoutOvercrowded(publish_stream->socket(), msg) != 0) {
        PLOG(WARNING) << publish_stream->remote_side() << '['
                      << publish_stream->stream_id() << "] Fail to respond publish";
    }
}

bool RtmpChunkStream::OnPublish(const RtmpMessageHeader& mh,
                                AMFInputStream* istream,
                                Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `publish'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read publish.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read publish.CommandObject";
        return false;
    }
    std::string publish_name;
    if (!ReadAMFString(&publish_name, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read publish.PublishName";
        return false;
    }
    std::string publish_type_str;
    if (!ReadAMFString(&publish_type_str, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read publish.PublishType";
        return false;
    }
    RtmpPublishType publish_type;
    if (!Str2RtmpPublishType(publish_type_str, &publish_type)) {
        RTMP_ERROR(socket, mh) << "Invalid publish_type=" << publish_type_str;
        return false;
    }

    RPC_VLOG << socket->remote_side() << "[" << mh.stream_id
             << "] publish{transaction_id=" << transaction_id
             << " stream_name=" << publish_name
             << " type=" << RtmpPublishType2Str(publish_type) << '}';

    butil::intrusive_ptr<RtmpStreamBase> stream_guard;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream_guard)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    // Change the chunk_stream_id of the server stream to be same with publish,
    // so that laterly user can call SendStopMessage successfully.
    stream_guard->_chunk_stream_id = chunk_stream_id();
    RtmpServerStream* stream = static_cast<RtmpServerStream*>(stream_guard.get());
    stream->_is_publish = true;
    OnPublishContinuation* done = new OnPublishContinuation;
    done->publish_name = publish_name;
    done->publish_stream.reset(stream, false/*not add ref*/);
    stream_guard.detach();
    stream->OnPublish(publish_name, publish_type, &done->status, done);
    return true;
}

static bool SendFMLEStartResponse(Socket* sock, double transaction_id) {
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_RESULT, &ostream);
        WriteAMFNumber(transaction_id, &ostream);
        WriteAMFNull(&ostream);
        WriteAMFUndefined(&ostream);
        CHECK(ostream.good());
    }
    SocketMessagePtr<RtmpUnsentMessage> msg(
        MakeUnsentControlMessage(RTMP_MESSAGE_COMMAND_AMF0, req_buf));
    if (sock->Write(msg) != 0) {
        PLOG(WARNING) << sock->remote_side() << ": Fail to respond FMLEStart";
        return false;
    }
    return true;
}

bool RtmpChunkStream::OnReleaseStream(
    const RtmpMessageHeader& mh, AMFInputStream* istream, Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `releaseStream'";
        return false;
    }
    double transaction_id = 0;
    if (!ReadAMFNumber(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read releaseStream.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read releaseStream.CommandObject";
        return false;
    }
    std::string stream_name;
    if (!ReadAMFString(&stream_name, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read releaseStream.StreamName";
        return false;
    }
    RTMP_WARNING(socket, mh) << "Ignored releaseStream(" << stream_name << ')';
    return SendFMLEStartResponse(socket, transaction_id);
}

bool RtmpChunkStream::OnFCPublish(
    const RtmpMessageHeader& mh, AMFInputStream* istream, Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `FCPublish'";
        return false;
    }
    double transaction_id = 0;
    if (!ReadAMFNumber(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read FCPublish.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read FCPublish.CommandObject";
        return false;
    }
    std::string stream_name;
    if (!ReadAMFString(&stream_name, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read FCPublish.StreamName";
        return false;
    }
    RTMP_WARNING(socket, mh) << "Ignored FCPublish(" << stream_name << ')';
    return SendFMLEStartResponse(socket, transaction_id);
}

bool RtmpChunkStream::OnFCUnpublish(
    const RtmpMessageHeader& mh, AMFInputStream* istream, Socket* socket) {
        if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `FCUnpublish'";
        return false;
    }
    double transaction_id = 0;
    if (!ReadAMFNumber(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read FCUnpublish.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read FCUnpublish.CommandObject";
        return false;
    }
    std::string stream_name;
    if (!ReadAMFString(&stream_name, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read FCUnpublish.StreamName";
        return false;
    }
    RTMP_WARNING(socket, mh) << "Ignored FCUnpublish(" << stream_name << ')';
    return SendFMLEStartResponse(socket, transaction_id);
}

bool RtmpChunkStream::OnGetStreamLength(
    const RtmpMessageHeader&, AMFInputStream*, Socket*) {
    // In (non-live) streams with no metadata, the duration of a stream can
    // be retrieved by calling the RTMP function getStreamLength with the
    // playpath. The server will return a positive duration upon the request if
    // the duration is known, otherwise either no response or a duration of 0
    // will be returned.

    // Just ignore the command.
    return true;
}

bool RtmpChunkStream::OnCheckBW(
    const RtmpMessageHeader&, AMFInputStream*, Socket*) {
    return true;
}

bool RtmpChunkStream::OnSeek(const RtmpMessageHeader& mh,
                             AMFInputStream* istream,
                             Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `seek'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read seek.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read seek.CommandObject";
        return false;
    }
    double milliseconds = 0;
    if (!ReadAMFNumber(&milliseconds, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read seek.milliSeconds";
        return false;
    }

    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    // TODO(gejun): Run in execq.
    int rc = static_cast<RtmpServerStream*>(stream.get())->OnSeek(milliseconds);
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        if (rc == 0) {
            WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
            WriteAMFUint32(0, &ostream);
            WriteAMFNull(&ostream);
            RtmpInfo info;
            info.set_code(RTMP_STATUS_CODE_STREAM_SEEK);
            info.set_level(RTMP_INFO_LEVEL_STATUS);
            info.set_description("Seek successfully.");
            WriteAMFObject(info, &ostream);
            CHECK(ostream.good());
        } else {
            WriteAMFString(RTMP_AMF0_COMMAND_ERROR, &ostream);
            WriteAMFNumber(0, &ostream);
            WriteAMFNull(&ostream);
            RtmpInfo info;
            info.set_level(RTMP_INFO_LEVEL_ERROR);
            info.set_code(RTMP_STATUS_CODE_STREAM_SEEK); // TODO
            info.set_description("Fail to seek");
            WriteAMFObject(info, &ostream);
            CHECK(ostream.good());
        }
    }
    SocketMessagePtr<RtmpUnsentMessage> msg(new RtmpUnsentMessage);
    msg->header.message_length = req_buf.size();
    msg->header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    msg->header.stream_id = mh.stream_id;
    msg->chunk_stream_id = chunk_stream_id();
    msg->body = req_buf;

    if (socket->Write(msg) != 0) {
        PLOG(WARNING) << socket->remote_side() << ": Fail to respond seek";
        return false;
    }
    return (rc == 0);
}

bool RtmpChunkStream::OnPause(const RtmpMessageHeader& mh,
                              AMFInputStream* istream,
                              Socket* socket) {
    if (!connection_context()->is_server_side()) {
        RTMP_ERROR(socket, mh) << "Client should not receive `pause'";
        return false;
    }
    uint32_t transaction_id = 0;
    if (!ReadAMFUint32(&transaction_id, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read pause.TransactionId";
        return false;
    }
    if (!ReadAMFNull(istream)) { // command object
        RTMP_ERROR(socket, mh) << "Fail to read pause.CommandObject";
        return false;
    }
    bool pause_or_unpause = true;
    if (!ReadAMFBool(&pause_or_unpause, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read pause/unpause flag";
        return false;
    }
    double milliseconds = 0;
    if (!ReadAMFNumber(&milliseconds, istream)) {
        RTMP_ERROR(socket, mh) << "Fail to read pause.milliSeconds";
        return false;
    }

    butil::intrusive_ptr<RtmpStreamBase> stream;
    if (!connection_context()->FindMessageStream(mh.stream_id, &stream)) {
        RTMP_WARNING(socket, mh) << "Fail to find stream_id=" << mh.stream_id;
        return false;
    }
    if (stream->_paused == pause_or_unpause) {
        if (pause_or_unpause) {
            RTMP_ERROR(socket, mh) << "Pause an already paused stream";
        } else {
            RTMP_ERROR(socket, mh) << "Unpause an already unpaused stream";
        }
        return false;
    }
    int rc = static_cast<RtmpServerStream*>(stream.get())->OnPause(
        pause_or_unpause, milliseconds);

    // Send back status.
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        if (rc == 0) {
            WriteAMFString(RTMP_AMF0_COMMAND_ON_STATUS, &ostream);
            WriteAMFUint32(0, &ostream);
            WriteAMFNull(&ostream);
            RtmpInfo info;
            if (pause_or_unpause) {
                info.set_code(RTMP_STATUS_CODE_STREAM_PAUSE);
            } else {
                info.set_code(RTMP_STATUS_CODE_STREAM_UNPAUSE);
            }
            info.set_level(RTMP_INFO_LEVEL_STATUS);
            info.set_description("Paused stream.");
            WriteAMFObject(info, &ostream);
            CHECK(ostream.good());
        } else {
            WriteAMFString(RTMP_AMF0_COMMAND_ERROR, &ostream);
            WriteAMFNumber(0, &ostream);
            WriteAMFNull(&ostream);
            RtmpInfo info;
            if (pause_or_unpause) {
                info.set_code(RTMP_STATUS_CODE_STREAM_PAUSE);
            } else {
                info.set_code(RTMP_STATUS_CODE_STREAM_UNPAUSE);
            }
            info.set_level(RTMP_INFO_LEVEL_ERROR);
            info.set_description(pause_or_unpause ? "Fail to pause" :
                                 "Fail to unpause");
            WriteAMFObject(info, &ostream);
            CHECK(ostream.good());
        }
    }
    SocketMessagePtr<RtmpUnsentMessage> msg1(new RtmpUnsentMessage);
    msg1->header.message_length = req_buf.size();
    msg1->header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    msg1->header.stream_id = mh.stream_id;
    msg1->chunk_stream_id = chunk_stream_id();
    msg1->body = req_buf;

    // StreamEOF(pause) or StreamBegin(unpause)
    char cntl_buf[6];
    char* p = cntl_buf;
    if (pause_or_unpause) {
        WriteBigEndian2Bytes(&p, RTMP_USER_CONTROL_EVENT_STREAM_EOF);
    } else {
        WriteBigEndian2Bytes(&p, RTMP_USER_CONTROL_EVENT_STREAM_BEGIN);
    }
    WriteBigEndian4Bytes(&p, mh.stream_id);
    RtmpUnsentMessage* msg2 = MakeUnsentControlMessage(
        RTMP_MESSAGE_USER_CONTROL, cntl_buf, sizeof(cntl_buf));
    msg1->next.reset(msg2);
    
    if (WriteWithoutOvercrowded(socket, msg1) != 0) {
        PLOG(WARNING) << socket->remote_side() << '[' << mh.stream_id
                      << "] Fail to respond " << (pause_or_unpause ? "pause" : "unpause");
        return false;
    }
    if (rc == 0) {
        stream->_paused = pause_or_unpause;
        return true;
    }
    return false;
}

// ============== protocol handlers =============

inline ParseResult IsPossiblyRtmp(const butil::IOBuf* source) {
    const char* p = (const char*)source->fetch1();
    if (p == NULL) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (*p != RTMP_DEFAULT_VERSION) {
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    return MakeMessage(NULL);
}

ParseResult ParseRtmpMessage(butil::IOBuf* source, Socket *socket, bool read_eof,
                             const void* arg) {
    RtmpContext* rtmp_ctx = static_cast<RtmpContext*>(socket->parsing_context());
    if (rtmp_ctx == NULL) {
        if (arg == NULL) {
            // We are probably parsing another client-side protocol.
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        const Server* server = static_cast<const Server*>(arg);
        RtmpService* service = server->options().rtmp_service;
        if (service == NULL) {
            // Validating RTMP protocol only checks the first byte, which
            // is very easy to be confused with other protocols. Currently
            // if rtmp_service is not set, the protocol is skipped w/o any
            // warning. We'll register the protocol on demand in later CI
            // to avoid the confusion from root.
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        if (read_eof) {
            // No former RtmpContext when we read an EOF
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        const ParseResult r = IsPossiblyRtmp(source);
        if (!r.is_ok()) {
            return r;
        }
        rtmp_ctx = new (std::nothrow) RtmpContext(NULL, server);
        if (rtmp_ctx == NULL) {
            LOG(FATAL) << "Fail to new RtmpContext";
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        socket->reset_parsing_context(rtmp_ctx);
        // We don't need to customize app_connect at server-side.
    }
    return rtmp_ctx->Feed(source, socket);
}

void ProcessRtmpMessage(InputMessageBase*) {
    CHECK(false) << "Should never be called";
}

class OnServerStreamCreated : public RtmpTransactionHandler {
public:
    OnServerStreamCreated(RtmpClientStream* stream, CallId call_id);
    void Run(bool error, const RtmpMessageHeader& mh,
             AMFInputStream* istream, Socket* socket);
    void Cancel();
private:
    butil::intrusive_ptr<RtmpClientStream> _stream;
    CallId _call_id;
};

OnServerStreamCreated::OnServerStreamCreated(
    RtmpClientStream* stream, CallId call_id)
    : _stream(stream), _call_id(call_id) {}

void OnServerStreamCreated::Run(bool error,
                                const RtmpMessageHeader&,
                                AMFInputStream* istream,
                                Socket* socket) {
    std::unique_ptr<OnServerStreamCreated> delete_self(this);
    // End the createStream call.
    RtmpContext* ctx = static_cast<RtmpContext*>(socket->parsing_context());
    if (ctx == NULL) {
        LOG(FATAL) << "RtmpContext must be created";
        return;
    }
    const int64_t start_parse_us = butil::cpuwide_time_us();
    // TODO(gejun): Don't have received time right now.
    const int64_t received_us = start_parse_us;
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    const bthread_id_t cid = _call_id;
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        return;
    }
    
    ControllerPrivateAccessor accessor(cntl);
    const int saved_error = cntl->ErrorCode();
    do {
        AMFObject cmd_obj;
        if (!ReadAMFObject(&cmd_obj, istream)) {
            cntl->SetFailed(ERESPONSE, "Fail to read the command object");
            break;
        }
        const AMFField* field = cmd_obj.Find("PlayOrPublishAccepted");
        if (field != NULL && field->IsBool() && field->AsBool()) {
            _stream->_created_stream_with_play_or_publish = true;
        }
        if (error) {
            RtmpInfo info;
            if (!ReadAMFObject(&info, istream)) {
                cntl->SetFailed(ERESPONSE, "Fail to read the info object");
                break;
            }
            cntl->SetFailed(ERTMPCREATESTREAM, "%s: %s", info.code().c_str(),
                            info.description().c_str());
            break;
        }
        uint32_t stream_id = 0;
        if (!ReadAMFUint32(&stream_id, istream)) {
            cntl->SetFailed(ERESPONSE, "Fail to read stream_id");
            break;
        }
        _stream->_message_stream_id = stream_id;
        // client stream needs to be added here rather than OnDestroyingStream
        // to avoid the race between OnDestroyingStream and a failed OnStatus,
        // because the former function runs in another bthread and may run later
        // than OnStatus which needs to see the stream.
        if (!ctx->AddClientStream(_stream.get())) {
            cntl->SetFailed(EINVAL, "Fail to add client stream_id=%u", stream_id);
            break;
        }
    } while (0);
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(base_realtime);
        span->set_received_us(received_us);
        span->set_response_size(istream->popped_bytes());
        span->set_start_parse_us(start_parse_us);
    }
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails.
    // Can't call accessor.OnResponse which does not new bthread.
    const Controller::CompletionInfo info = { cid, true };
    cntl->OnVersionedRPCReturned(info, true, saved_error);
}

void OnServerStreamCreated::Cancel() {
    delete this;
}

butil::Status
RtmpCreateStreamMessage::AppendAndDestroySelf(butil::IOBuf* out, Socket* s) {
    std::unique_ptr<RtmpCreateStreamMessage> destroy_self(this);
    if (s == NULL) {  // abandoned
        return butil::Status::OK();
    }
    // Serialize createStream command
    RtmpContext* ctx = static_cast<RtmpContext*>(socket->parsing_context());
    if (ctx == NULL) {
        return butil::Status(EINVAL, "RtmpContext of %s is not created",
                            socket->description().c_str());
    }
    butil::IOBuf req_buf;
    {
        butil::IOBufAsZeroCopyOutputStream zc_stream(&req_buf);
        AMFOutputStream ostream(&zc_stream);
        WriteAMFString(RTMP_AMF0_COMMAND_CREATE_STREAM, &ostream);
        WriteAMFUint32(transaction_id, &ostream);
        // Notice that we can't directly pack play/publish command as the
        // command object, because SRS at source site(for publishing) only
        // accepts null command objects, although non-null command objects
        // are allowed in RTMP spec.
        // One limitation of current implementation is that even if the server
        // accepts play/publish along with createStream, the first createStream
        // command will not carry play/publish information, because connection
        // is established lazily, when first createStream is being packed,
        // connect command is not sent yet and capability of the server is
        // unknown.
        if (ctx->can_stream_be_created_with_play_or_publish()) {
            AMFObject cmd_obj;
            if (!options.publish_name.empty()) {
                cmd_obj.SetString("CommandName", "publish");
                cmd_obj.SetString("StreamName", options.publish_name);
                cmd_obj.SetString("PublishType",
                                  RtmpPublishType2Str(options.publish_type));
                WriteAMFObject(cmd_obj, &ostream);
            } else if (!options.play_name.empty()) {
                cmd_obj.SetString("CommandName", "play");
                cmd_obj.SetString("StreamName", options.play_name);
                WriteAMFObject(cmd_obj, &ostream);
            } else {
                WriteAMFNull(&ostream);
            }
        } else {
            WriteAMFNull(&ostream);
        }
        CHECK(ostream.good());
    }
    RtmpChunkStream* cstream = ctx->GetChunkStream(RTMP_CONTROL_CHUNK_STREAM_ID);
    if (cstream == NULL) {
        socket->SetFailed(EINVAL, "Invalid chunk_stream_id=%u",
                          RTMP_CONTROL_CHUNK_STREAM_ID);
        return butil::Status(EINVAL, "Invalid chunk_stream_id=%u",
                            RTMP_CONTROL_CHUNK_STREAM_ID);
    }
    RtmpMessageHeader header;
    header.message_length = req_buf.size();
    header.message_type = RTMP_MESSAGE_COMMAND_AMF0;
    header.stream_id = RTMP_CONTROL_MESSAGE_STREAM_ID;
    if (cstream->SerializeMessage(out, header, &req_buf) != 0) {
        socket->SetFailed(EINVAL, "Fail to serialize message");
        return butil::Status(EINVAL, "Fail to serialize message");
    }
    return butil::Status::OK();
}

void PackRtmpRequest(butil::IOBuf* /*buf*/,
                     SocketMessage** user_message,
                     uint64_t /*correlation_id*/,
                     const google::protobuf::MethodDescriptor* /*NULL*/,
                     Controller* cntl,
                     const butil::IOBuf& /*request*/,
                     const Authenticator*) {
    // Send createStream command
    ControllerPrivateAccessor accessor(cntl);
    Socket* s = accessor.get_sending_socket();
    RtmpContext* ctx = static_cast<RtmpContext*>(s->parsing_context());
    if (ctx == NULL) {
        cntl->SetFailed(EINVAL, "RtmpContext of %s is not created",
                        s->description().c_str());
        return;
    }
    // Hack: we pass stream as response in RtmpClientStream::Create
    RtmpClientStream* stream = (RtmpClientStream*)cntl->response();

    // Hack: save last transaction_id into log_id(useless here) so that we
    // can get it back and cancel the transaction before creating new one
    // (for retrying).
    CHECK_LT(cntl->log_id(), (uint64_t)std::numeric_limits<uint32_t>::max());
    uint32_t transaction_id = cntl->log_id();
    if (transaction_id != 0) {
        RtmpTransactionHandler* last_handler =
            ctx->RemoveTransaction(transaction_id);
        if (last_handler) {
            last_handler->Cancel();
        }
    }
    OnServerStreamCreated* cb = new OnServerStreamCreated(stream, cntl->call_id());
    if (!ctx->AddTransaction(&transaction_id, cb)) {
        cntl->SetFailed(EINVAL, "Fail to add transaction");
        delete cb;
        return;
    }
    cntl->set_log_id(transaction_id);
    RtmpCreateStreamMessage* msg = new RtmpCreateStreamMessage;
    s->ReAddress(&msg->socket);
    msg->transaction_id = transaction_id;
    msg->options = stream->options();
    *user_message = msg;
}

void SerializeRtmpRequest(butil::IOBuf* /*buf*/,
                          Controller* /*cntl*/,
                          const google::protobuf::Message* /*NULL*/) {
}

}  // namespace policy
} // namespace brpc


#undef RTMP_LOG
#undef RTMP_ERROR
#undef RTMP_WARNING
