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

    // the sequence id of the request packet from the client
    int8_t sequence_id;
    std::string current_db;
    std::string scramble;
};

class MysqlProtocolPacketHeader {
public:
    MysqlProtocolPacketHeader() :
        payload_length_{0U, 0U, 0U},
        sequence_id_(0U) { }

    void SetPayloadLength(uint32_t payload_length) {
        butil::IntStore3Bytes(payload_length_, payload_length);
    }

    void SetSequenceId(uint8_t sequence_id) {
        sequence_id_ = sequence_id;
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

    void AppendInt(uint32_t value) {
        char buf[4];
        butil::IntStore4Bytes(buf, value);
        buf_.append(buf, sizeof(buf));
    }

    void AppendInt(uint16_t value) {
        char buf[2];
        butil::IntStore2Bytes(buf, value);
        buf_.append(buf, sizeof(buf));
    }

    size_t length() const {
        return buf_.length();
    }

private:
    butil::IOBuf buf_;
};

} // namespace policy
} // namespace brpc

#endif // BRPC_POLICY_MYSQL_PROTOCOL_H
