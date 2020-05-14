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

#ifndef BRPC_POLICY_MYSQL_PROTOCOL_H
#define BRPC_POLICY_MYSQL_PROTOCOL_H

#include <unistd.h>
#include <sys/types.h>

#include "butil/iobuf.h"
#include "brpc/destroyable.h"
#include "brpc/protocol.h"
#include "brpc/policy/mysql_meta.pb.h"
#include "butil/sys_byteorder.h"


namespace brpc {
namespace policy {

void ServerSendInitialPacketDemo(Socket* socket);

ParseResult ParseMysqlMessage(
        butil::IOBuf* source,
        Socket* socket,
        bool read_eof,
        const void *arg);

void ProcessMysqlRequest(InputMessageBase* msg_base);

bool VerifyMysqlRequest(const InputMessageBase* msg_base);

// This class is as parsing_context in socket.
class MysqlConnContext : public Destroyable  {
public:
    MysqlConnContext() :
        sequence_id(0) { }
    explicit MysqlConnContext(uint8_t seq_id) :
        sequence_id(seq_id) {}

    ~MysqlConnContext() { }
    // @Destroyable
    void Destroy() override {
        delete this;
    }

    int8_t NextSequenceId() {
        return ++sequence_id;
    }

    void ResetSequenceId(uint8_t seq_id) {
        sequence_id = seq_id;
    }

    void SetCurrentDB(const std::string& db_name) {
        current_db = db_name;
    }

    butil::StringPiece CurrentDB() {
        return butil::StringPiece(current_db);
    }

    void SetScramble(const std::string& auth_plugin_data) {
        scramble = auth_plugin_data;
    }

    butil::StringPiece Scramble() {
        return butil::StringPiece(scramble);
    }

    void SetConnectionId(uint32_t conn_id_) {
        this->conn_id = conn_id_;
    }

    uint32_t ConnectionId() const {
        return conn_id;
    }

    // the sequence id of the request packet from the client
    int8_t sequence_id;
    std::string current_db;
    std::string scramble;
    uint32_t conn_id;
};

class MysqlProtocolPacketHeader {
public:
    MysqlProtocolPacketHeader() :
        payload_length_{0U, 0U, 0U},
        sequence_id_(0U) { }

    MysqlProtocolPacketHeader& SetPayloadLength(uint32_t payload_length) {
        butil::IntStore3Bytes(payload_length_, payload_length);
        return *this;
    }

    MysqlProtocolPacketHeader& SetSequenceId(uint8_t sequence_id) {
        sequence_id_ = sequence_id;
        return *this;
    }

    void AppendToIOBuf(butil::IOBuf& iobuf) const {
        iobuf.append(payload_length_, 4);
    }
private:
    uint8_t payload_length_[3];
    uint8_t sequence_id_;
};

class MysqlProtocolPacketBody {
public:
    butil::IOBuf& buf() {
        return buf_;
    }

    void Append(const void* data, size_t count) {
        buf_.append(data, count);
    }

    void AppendByte(uint8_t value) {
        buf_.append(&value, 1);
    }

    void AppendString(const std::string& str) {
        buf_.append(str);
    }

    void AppendLenencStr(const std::string& str) {
        AppendLenencInt(str.length());
        AppendString(str);
    }

    void AppendLenencInt(uint64_t value) {
        if (value < 251) {
            AppendInt((uint8_t)value);
            return;
        } else if (value < 2 << 16) {
            AppendByte(0xfc);
            AppendInt((uint16_t)value);
            return;
        } else if (value < 2 << 24) {
            AppendByte(0xfd);
            char buf[3];
            butil::IntStore3Bytes(buf, (int32_t)value);
            buf_.append(buf, sizeof(buf));
            return;
        } else if (value < 2 << 64 ) {
            AppendByte(0xfe);
            AppendInt(value);
        }
    }
    void AppendInt(uint8_t value) {
        buf_.append(&value, 1);
    }
    void AppendInt(uint16_t value) {
        char buf[2];
        butil::IntStore2Bytes(buf, value);
        buf_.append(buf, sizeof(buf));
    }
    void AppendInt(uint32_t value) {
        char buf[4];
        butil::IntStore4Bytes(buf, value);
        buf_.append(buf, sizeof(buf));
    }
    void AppendInt(uint64_t value) {
        char buf[8];
        butil::IntStore8Bytes(buf, value);
        buf_.append(buf, sizeof(buf));
    }

    size_t length() const {
        return buf_.length();
    }

private:
    butil::IOBuf buf_;
};

class MysqlRawUnpacker {
public:
    explicit MysqlRawUnpacker(const void* stream) :
        stream_((const char*)stream) { }

    MysqlRawUnpacker& unpack_uint1(uint8_t& hostvalue) {
        hostvalue = *((const uint8_t*)stream_);
        stream_ += 1;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint2(uint16_t& hostname) {
        hostname = butil::UnsignedIntLoad2Bytes(stream_);
        stream_ += 2;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint3(uint32_t& hostvalue) {
        hostvalue = butil::UnsignedIntLoad3Bytes(stream_);
        stream_ += 3;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint4(uint32_t& hostvalue) {
        hostvalue = butil::UnsignedIntLoad4Bytes(stream_);
        stream_ += 4;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint8(uint64_t& hostvalue) {
        hostvalue = butil::UnsignedIntLoad8Bytes(stream_);
        stream_ += 8;
        return *this;
    }
    MysqlRawUnpacker& unpack_cstr(std::string& strval) {
        while (*stream_) {
            strval.append(stream_, 1);
            stream_ += 1;
        }
        stream_ += 1; // skip the NULL
        return *this;
    }
    MysqlRawUnpacker& unpack_lenenc_str(std::string& strval) {
        uint8_t first_byte = 0U;
        uint64_t real_length = 0U;
        this->unpack_uint1(first_byte);

        if (first_byte >= 0U && first_byte < 251U) {
            real_length = first_byte;
        } else if (first_byte == 0xFC) {
            uint16_t t;
            this->unpack_uint2(t);
            real_length = t;
        } else if (first_byte == 0xFD) {
            uint32_t t;
            this->unpack_uint3(t);
            real_length = t;
        } else if (first_byte == 0xFE) {
            this->unpack_uint8(real_length);
        } else {
            // impossble
        }

        this->unpack_strn(strval, real_length);
        return *this;
    }
    MysqlRawUnpacker& unpack_strn(std::string& strval, size_t n) {
        strval.append(stream_, n);
        stream_ += n;
        return *this;
    }
    MysqlRawUnpacker& skipn(size_t n) {
        stream_ += n;
        return *this;
    }
private:
    const char* stream_;
};

} // namespace policy
} // namespace brpc

#endif // BRPC_POLICY_MYSQL_PROTOCOL_H
