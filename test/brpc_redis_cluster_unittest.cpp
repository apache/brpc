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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "bthread/bthread.h"
#include <gtest/gtest.h>

#include "bthread/countdown_event.h"
#include "butil/synchronization/lock.h"
#include "brpc/channel.h"
#include "brpc/redis.h"
#include "brpc/redis_cluster.h"
#include "brpc/server.h"

namespace {

const int kSplitSlot = 8191;

uint16_t HashSlot(const std::string& key) {
    // Keep this aligned with implementation in redis_cluster.cpp.
    static const uint16_t table[256] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
        0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
        0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
        0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
        0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
        0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
        0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
        0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
        0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
        0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
        0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
        0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
        0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
        0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
        0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
        0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
        0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
        0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
        0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
        0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
        0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
        0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
        0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
        0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
        0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
        0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
        0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
        0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
        0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
        0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
        0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0
    };

    std::string hashed = key;
    size_t begin = key.find('{');
    if (begin != std::string::npos) {
        size_t end = key.find('}', begin + 1);
        if (end != std::string::npos && end > begin + 1) {
            hashed = key.substr(begin + 1, end - begin - 1);
        }
    }
    uint16_t crc = 0;
    for (size_t i = 0; i < hashed.size(); ++i) {
        uint8_t idx = static_cast<uint8_t>((crc >> 8) ^
                      static_cast<uint8_t>(hashed[i]));
        crc = static_cast<uint16_t>((crc << 8) ^ table[idx]);
    }
    return crc & 16383;
}

int OwnerBySlot(int slot) {
    return slot <= kSplitSlot ? 0 : 1;
}

struct ClusterMeta {
    std::string endpoint[2];
    bool fail_slots;
    bool fail_nodes;
    bool slots_empty_host;
    std::unordered_map<std::string, int> owner_override;
    std::unordered_map<std::string, std::string> forced_error_by_key;
    std::atomic<int> slots_calls;
    std::atomic<int> nodes_calls;
    std::atomic<int> moved_error_calls;
    std::atomic<int> ask_error_calls;
    std::string custom_nodes_payload;

    bool enable_ask;
    std::string ask_key;
    int ask_from;
    int ask_to;

    std::string redirect_loop_key;

    ClusterMeta()
        : fail_slots(false)
        , fail_nodes(false)
        , slots_empty_host(false)
        , slots_calls(0)
        , nodes_calls(0)
        , moved_error_calls(0)
        , ask_error_calls(0)
        , enable_ask(false)
        , ask_from(0)
        , ask_to(1) {
    }

    int OwnerOfKey(const std::string& key) const {
        std::unordered_map<std::string, int>::const_iterator it = owner_override.find(key);
        if (it != owner_override.end()) {
            return it->second;
        }
        return OwnerBySlot(HashSlot(key));
    }
};

struct NodeData {
    int node_id;
    ClusterMeta* meta;
    butil::Mutex mutex;
    std::unordered_map<std::string, std::string> kv;
};

class Session : public brpc::Destroyable {
public:
    Session() : asking(false) {}
    void Destroy() override { delete this; }
    bool asking;
};

static Session* GetOrCreateSession(brpc::RedisConnContext* ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    Session* s = static_cast<Session*>(ctx->get_session());
    if (s == NULL) {
        s = new Session;
        ctx->reset_session(s);
    }
    return s;
}

static bool ParseEndpoint(const std::string& endpoint, std::string* host, int* port) {
    size_t pos = endpoint.rfind(':');
    if (pos == std::string::npos) {
        return false;
    }
    *host = endpoint.substr(0, pos);
    *port = atoi(endpoint.substr(pos + 1).c_str());
    return (*port > 0);
}

class AskingHandler : public brpc::RedisCommandHandler {
public:
    brpc::RedisCommandHandlerResult Run(brpc::RedisConnContext* ctx,
                                        const std::vector<butil::StringPiece>& /*args*/,
                                        brpc::RedisReply* output,
                                        bool /*flush_batched*/) override {
        Session* s = GetOrCreateSession(ctx);
        if (s != NULL) {
            s->asking = true;
        }
        output->SetStatus("OK");
        return brpc::REDIS_CMD_HANDLED;
    }
};

class ClusterCommandHandler : public brpc::RedisCommandHandler {
public:
    explicit ClusterCommandHandler(ClusterMeta* meta) : _meta(meta) {}

    brpc::RedisCommandHandlerResult Run(brpc::RedisConnContext* /*ctx*/,
                                        const std::vector<butil::StringPiece>& args,
                                        brpc::RedisReply* output,
                                        bool /*flush_batched*/) override {
        if (args.size() < 2) {
            output->SetError("ERR wrong number of arguments for 'cluster' command");
            return brpc::REDIS_CMD_HANDLED;
        }
        if (args[1] == "slots") {
            _meta->slots_calls.fetch_add(1, std::memory_order_relaxed);
            if (_meta->fail_slots) {
                output->SetError("ERR cluster slots disabled for test");
                return brpc::REDIS_CMD_HANDLED;
            }
            output->SetArray(2);
            FillSlotEntry((*output)[0], 0, kSplitSlot, _meta->endpoint[0],
                          _meta->slots_empty_host);
            FillSlotEntry((*output)[1], kSplitSlot + 1, 16383, _meta->endpoint[1],
                          _meta->slots_empty_host);
            return brpc::REDIS_CMD_HANDLED;
        }
        if (args[1] == "nodes") {
            _meta->nodes_calls.fetch_add(1, std::memory_order_relaxed);
            if (_meta->fail_nodes) {
                output->SetError("ERR cluster nodes disabled for test");
                return brpc::REDIS_CMD_HANDLED;
            }
            if (!_meta->custom_nodes_payload.empty()) {
                output->SetString(_meta->custom_nodes_payload);
                return brpc::REDIS_CMD_HANDLED;
            }
            std::ostringstream oss;
            oss << "node0 " << _meta->endpoint[0] << "@17000 master - 0 0 1 connected 0-"
                << kSplitSlot << "\n";
            oss << "node1 " << _meta->endpoint[1] << "@17001 master - 0 0 1 connected "
                << (kSplitSlot + 1) << "-16383\n";
            output->SetString(oss.str());
            return brpc::REDIS_CMD_HANDLED;
        }
        output->SetError("ERR unsupported CLUSTER subcommand");
        return brpc::REDIS_CMD_HANDLED;
    }

private:
    static void FillSlotEntry(brpc::RedisReply& reply, int start, int end,
                              const std::string& endpoint,
                              bool empty_host) {
        std::string host;
        int port = 0;
        ParseEndpoint(endpoint, &host, &port);
        reply.SetArray(3);
        reply[0].SetInteger(start);
        reply[1].SetInteger(end);
        reply[2].SetArray(2);
        if (empty_host) {
            reply[2][0].SetString("");
        } else {
            reply[2][0].SetString(host);
        }
        reply[2][1].SetInteger(port);
    }

private:
    ClusterMeta* _meta;
};

class KVCommandHandler : public brpc::RedisCommandHandler {
public:
    explicit KVCommandHandler(NodeData* data) : _data(data) {}

    brpc::RedisCommandHandlerResult Run(brpc::RedisConnContext* ctx,
                                        const std::vector<butil::StringPiece>& args,
                                        brpc::RedisReply* output,
                                        bool /*flush_batched*/) override {
        if (args.empty()) {
            output->SetError("ERR empty command");
            return brpc::REDIS_CMD_HANDLED;
        }
        const std::string command = args[0].as_string();
        if (command == "ping") {
            output->SetStatus("PONG");
            return brpc::REDIS_CMD_HANDLED;
        }
        if (command == "eval" || command == "evalsha") {
            output->SetStatus("OK");
            return brpc::REDIS_CMD_HANDLED;
        }
        if (args.size() < 2) {
            output->SetError("ERR wrong number of arguments");
            return brpc::REDIS_CMD_HANDLED;
        }

        const std::string key = args[1].as_string();
        const int slot = HashSlot(key);
        const int owner = _data->meta->OwnerOfKey(key);

        std::unordered_map<std::string, std::string>::const_iterator forced =
            _data->meta->forced_error_by_key.find(key);
        if (forced != _data->meta->forced_error_by_key.end()) {
            output->SetError(forced->second);
            return brpc::REDIS_CMD_HANDLED;
        }

        if (!_data->meta->redirect_loop_key.empty() &&
            key == _data->meta->redirect_loop_key) {
            const int target = 1 - _data->node_id;
            _data->meta->moved_error_calls.fetch_add(1, std::memory_order_relaxed);
            output->FormatError("MOVED %d %s", slot,
                                _data->meta->endpoint[target].c_str());
            return brpc::REDIS_CMD_HANDLED;
        }

        bool bypass_owner_check = false;
        if (_data->meta->enable_ask && key == _data->meta->ask_key) {
            if (_data->node_id == _data->meta->ask_from) {
                _data->meta->ask_error_calls.fetch_add(1, std::memory_order_relaxed);
                output->FormatError("ASK %d %s", slot,
                                    _data->meta->endpoint[_data->meta->ask_to].c_str());
                return brpc::REDIS_CMD_HANDLED;
            }
            if (_data->node_id == _data->meta->ask_to) {
                Session* s = GetOrCreateSession(ctx);
                if (s == NULL || !s->asking) {
                    output->SetError("ERR ASKING required");
                    return brpc::REDIS_CMD_HANDLED;
                }
                s->asking = false;
                bypass_owner_check = true;
            }
        }

        if (!bypass_owner_check && owner != _data->node_id) {
            _data->meta->moved_error_calls.fetch_add(1, std::memory_order_relaxed);
            output->FormatError("MOVED %d %s", slot, _data->meta->endpoint[owner].c_str());
            return brpc::REDIS_CMD_HANDLED;
        }

        if (command == "set") {
            if (args.size() < 3) {
                output->SetError("ERR wrong number of arguments for 'set' command");
                return brpc::REDIS_CMD_HANDLED;
            }
            BAIDU_SCOPED_LOCK(_data->mutex);
            _data->kv[key] = args[2].as_string();
            output->SetStatus("OK");
            return brpc::REDIS_CMD_HANDLED;
        }
        if (command == "get") {
            BAIDU_SCOPED_LOCK(_data->mutex);
            std::unordered_map<std::string, std::string>::iterator it = _data->kv.find(key);
            if (it == _data->kv.end()) {
                output->SetNullString();
            } else {
                output->SetString(it->second);
            }
            return brpc::REDIS_CMD_HANDLED;
        }
        if (command == "del" || command == "unlink") {
            BAIDU_SCOPED_LOCK(_data->mutex);
            output->SetInteger(_data->kv.erase(key) ? 1 : 0);
            return brpc::REDIS_CMD_HANDLED;
        }
        if (command == "exists") {
            BAIDU_SCOPED_LOCK(_data->mutex);
            output->SetInteger(_data->kv.count(key) ? 1 : 0);
            return brpc::REDIS_CMD_HANDLED;
        }

        output->SetError("ERR unsupported command");
        return brpc::REDIS_CMD_HANDLED;
    }

private:
    NodeData* _data;
};

class ClusterRedisService : public brpc::RedisService {
public:
    explicit ClusterRedisService(NodeData* data) {
        AddCommandHandler("asking", new AskingHandler());
        AddCommandHandler("cluster", new ClusterCommandHandler(data->meta));

        KVCommandHandler* handler = new KVCommandHandler(data);
        AddCommandHandler("ping", handler);
        AddCommandHandler("get", handler);
        AddCommandHandler("set", handler);
        AddCommandHandler("del", handler);
        AddCommandHandler("exists", handler);
        AddCommandHandler("unlink", handler);
        AddCommandHandler("eval", handler);
        AddCommandHandler("evalsha", handler);
    }
};

class Done : public google::protobuf::Closure {
public:
    explicit Done(bthread::CountdownEvent* e) : _event(e) {}
    void Run() override { _event->signal(); }
private:
    bthread::CountdownEvent* _event;
};

class RedisClusterChannelTest : public testing::Test {
protected:
    void SetUp() override {
        _meta.reset(new ClusterMeta);
        _node[0].meta = _meta.get();
        _node[0].node_id = 0;
        _node[1].meta = _meta.get();
        _node[1].node_id = 1;

        StartServer(0);
        StartServer(1);
    }

    void TearDown() override {
        for (int i = 0; i < 2; ++i) {
            _server[i].Stop(0);
        }
        for (int i = 0; i < 2; ++i) {
            _server[i].Join();
        }
    }

    std::string SeedList() const {
        return _meta->endpoint[0] + "," + _meta->endpoint[1];
    }

    void InitChannel(brpc::RedisClusterChannel* channel, int max_redirect = 5) {
        brpc::RedisClusterChannelOptions options;
        options.max_redirect = max_redirect;
        options.enable_periodic_refresh = false;
        ASSERT_EQ(0, channel->Init(SeedList(), &options));
    }

    std::string FindKeyForNode(int node_id) const {
        for (int i = 0; i < 200000; ++i) {
            std::ostringstream oss;
            oss << "key_" << node_id << '_' << i;
            if (OwnerBySlot(HashSlot(oss.str())) == node_id) {
                return oss.str();
            }
        }
        return "fallback_key";
    }

    std::vector<std::string> FindKeysForNode(int node_id, size_t count) const {
        std::vector<std::string> keys;
        keys.reserve(count);
        for (int i = 0; i < 400000 && keys.size() < count; ++i) {
            std::ostringstream oss;
            oss << "key_batch_" << node_id << '_' << i;
            if (OwnerBySlot(HashSlot(oss.str())) == node_id) {
                keys.push_back(oss.str());
            }
        }
        return keys;
    }

    std::string FindHashTagForNode(int node_id) const {
        for (int i = 0; i < 200000; ++i) {
            std::ostringstream oss;
            oss << "tag_" << node_id << '_' << i;
            const std::string key = "{" + oss.str() + "}";
            if (OwnerBySlot(HashSlot(key)) == node_id) {
                return oss.str();
            }
        }
        return "fallback_tag";
    }

private:
    void StartServer(int index) {
        brpc::ServerOptions options;
        options.redis_service = new ClusterRedisService(&_node[index]);
        brpc::PortRange range(20000 + index * 1000, 20999 + index * 1000);
        ASSERT_EQ(0, _server[index].Start("127.0.0.1", range, &options));
        std::ostringstream oss;
        oss << "127.0.0.1:" << _server[index].listen_address().port;
        _meta->endpoint[index] = oss.str();
    }

protected:
    std::unique_ptr<ClusterMeta> _meta;
    NodeData _node[2];
    brpc::Server _server[2];
};

TEST_F(RedisClusterChannelTest, basic_routing_and_multi_key_commands) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key0 = FindKeyForNode(0);
    const std::string key1 = FindKeyForNode(1);

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("mset %s v0 %s v1", key0.c_str(), key1.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_string());
        ASSERT_EQ("OK", resp.reply(0).data());
    }

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("mget %s %s", key0.c_str(), key1.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_array());
        ASSERT_EQ(2u, resp.reply(0).size());
        ASSERT_EQ("v0", resp.reply(0)[0].data());
        ASSERT_EQ("v1", resp.reply(0)[1].data());
    }

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("exists %s %s", key0.c_str(), key1.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(2, resp.reply(0).integer());
    }

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("del %s %s", key0.c_str(), key1.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(2, resp.reply(0).integer());
    }
}

TEST_F(RedisClusterChannelTest, moved_redirection) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string moved_key = FindKeyForNode(0);
    _meta->owner_override[moved_key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[moved_key] = "moved-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", moved_key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("moved-value", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, ask_redirection) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    _meta->enable_ask = true;
    _meta->ask_from = 0;
    _meta->ask_to = 1;
    _meta->ask_key = FindKeyForNode(0);
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[_meta->ask_key] = "ask-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", _meta->ask_key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("ask-value", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, ask_redirection_does_not_override_slot_cache) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    _meta->enable_ask = true;
    _meta->ask_from = 0;
    _meta->ask_to = 1;
    _meta->ask_key = FindKeyForNode(0);
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[_meta->ask_key] = "ask-value";
    }

    for (int i = 0; i < 5; ++i) {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("get %s", _meta->ask_key.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_string());
        ASSERT_EQ("ask-value", resp.reply(0).data());
    }
}

TEST_F(RedisClusterChannelTest, cluster_nodes_fallback) {
    _meta->fail_slots = true;
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(1);
    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("set %s vv", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("OK", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, eval_and_evalsha) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key0 = FindKeyForNode(0);
    const std::string key1 = FindKeyForNode(1);

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        const butil::StringPiece parts[] = {
            "eval", "return 1", "2", key0, key1
        };
        ASSERT_TRUE(req.AddCommandByComponents(parts, sizeof(parts) / sizeof(parts[0])));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_error());
        ASSERT_NE(std::string::npos,
                  std::string(resp.reply(0).error_message()).find("CROSSSLOT"));
    }

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        ASSERT_TRUE(req.AddCommand("evalsha abcdef 1 %s arg1", key0.c_str()));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_string());
        ASSERT_EQ("OK", resp.reply(0).data());
    }
}

TEST_F(RedisClusterChannelTest, redirect_retry_limit) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel, 3);

    _meta->redirect_loop_key = FindKeyForNode(0);
    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", _meta->redirect_loop_key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_TRUE(cntl.Failed());
    ASSERT_NE(std::string::npos, cntl.ErrorText().find("redirect"));
}

TEST_F(RedisClusterChannelTest, async_call) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(1);
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key] = "async-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));

    bthread::CountdownEvent event(1);
    Done done(&event);
    channel.CallMethod(NULL, &cntl, &req, &resp, &done);
    event.wait();

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("async-value", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, pipeline_order_with_mixed_commands) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key0 = FindKeyForNode(0);
    const std::string key1 = FindKeyForNode(1);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("set %s va", key0.c_str()));
    ASSERT_TRUE(req.AddCommand("set %s vb", key1.c_str()));
    ASSERT_TRUE(req.AddCommand("mget %s %s", key0.c_str(), key1.c_str()));
    ASSERT_TRUE(req.AddCommand("exists %s %s", key0.c_str(), key1.c_str()));
    ASSERT_TRUE(req.AddCommand("unlink %s %s", key0.c_str(), key1.c_str()));
    ASSERT_TRUE(req.AddCommand("mget %s %s", key0.c_str(), key1.c_str()));

    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(6, resp.reply_size());
    ASSERT_EQ("OK", resp.reply(0).data());
    ASSERT_EQ("OK", resp.reply(1).data());

    ASSERT_TRUE(resp.reply(2).is_array());
    ASSERT_EQ(2u, resp.reply(2).size());
    ASSERT_EQ("va", resp.reply(2)[0].data());
    ASSERT_EQ("vb", resp.reply(2)[1].data());

    ASSERT_TRUE(resp.reply(3).is_integer());
    ASSERT_EQ(2, resp.reply(3).integer());
    ASSERT_TRUE(resp.reply(4).is_integer());
    ASSERT_EQ(2, resp.reply(4).integer());

    ASSERT_TRUE(resp.reply(5).is_array());
    ASSERT_EQ(2u, resp.reply(5).size());
    ASSERT_TRUE(resp.reply(5)[0].is_nil());
    ASSERT_TRUE(resp.reply(5)[1].is_nil());
}

TEST_F(RedisClusterChannelTest, transaction_commands_are_not_supported) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("multi"));
    ASSERT_TRUE(req.AddCommand("exec"));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(2, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_TRUE(resp.reply(1).is_error());
    ASSERT_NE(std::string::npos,
              std::string(resp.reply(0).error_message()).find("not supported"));
}

TEST_F(RedisClusterChannelTest, eval_argument_validation) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        const butil::StringPiece parts[] = {
            "eval", "return 1", "abc", "k1"
        };
        ASSERT_TRUE(req.AddCommandByComponents(parts, sizeof(parts) / sizeof(parts[0])));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_error());
        ASSERT_NE(std::string::npos,
                  std::string(resp.reply(0).error_message()).find("invalid numkeys"));
    }

    {
        brpc::RedisRequest req;
        brpc::RedisResponse resp;
        brpc::Controller cntl;
        const butil::StringPiece parts[] = {
            "eval", "return 1", "2", "k1"
        };
        ASSERT_TRUE(req.AddCommandByComponents(parts, sizeof(parts) / sizeof(parts[0])));
        channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, resp.reply_size());
        ASSERT_TRUE(resp.reply(0).is_error());
        ASSERT_NE(std::string::npos,
                  std::string(resp.reply(0).error_message()).find("not enough keys"));
    }
}

TEST_F(RedisClusterChannelTest, async_failure_propagation) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel, 1);
    _meta->redirect_loop_key = FindKeyForNode(0);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", _meta->redirect_loop_key.c_str()));

    bthread::CountdownEvent event(1);
    Done done(&event);
    channel.CallMethod(NULL, &cntl, &req, &resp, &done);
    event.wait();

    ASSERT_TRUE(cntl.Failed());
    ASSERT_NE(std::string::npos, cntl.ErrorText().find("redirect"));
}

TEST_F(RedisClusterChannelTest, max_redirect_zero_fails_on_single_redirect) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel, 0);

    const std::string key = FindKeyForNode(0);
    _meta->owner_override[key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key] = "value-on-node1";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_TRUE(cntl.Failed());
    ASSERT_NE(std::string::npos, cntl.ErrorText().find("redirect"));
}

TEST_F(RedisClusterChannelTest, redirect_with_refresh_failure_still_returns_reply) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(0);
    _meta->owner_override[key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key] = "moved-value";
    }

    const int before_slots = _meta->slots_calls.load(std::memory_order_relaxed);
    const int before_nodes = _meta->nodes_calls.load(std::memory_order_relaxed);
    _meta->fail_slots = true;
    _meta->fail_nodes = true;

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("moved-value", resp.reply(0).data());

    ASSERT_GT(_meta->slots_calls.load(std::memory_order_relaxed), before_slots);
    ASSERT_GT(_meta->nodes_calls.load(std::memory_order_relaxed), before_nodes);
}

TEST_F(RedisClusterChannelTest, pipeline_with_ask_and_moved_keeps_order) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string ask_key = FindKeyForNode(0);
    std::string moved_key;
    for (int i = 0; i < 200000; ++i) {
        std::ostringstream oss;
        oss << "moved_key_" << i;
        if (OwnerBySlot(HashSlot(oss.str())) == 0 && oss.str() != ask_key) {
            moved_key = oss.str();
            break;
        }
    }
    ASSERT_FALSE(moved_key.empty());

    _meta->enable_ask = true;
    _meta->ask_from = 0;
    _meta->ask_to = 1;
    _meta->ask_key = ask_key;
    _meta->owner_override[moved_key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[ask_key] = "ask-value";
        _node[1].kv[moved_key] = "moved-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", ask_key.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", moved_key.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", ask_key.c_str()));

    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(3, resp.reply_size());
    ASSERT_EQ("ask-value", resp.reply(0).data());
    ASSERT_EQ("moved-value", resp.reply(1).data());
    ASSERT_EQ("ask-value", resp.reply(2).data());
}

TEST_F(RedisClusterChannelTest, fallback_to_nodes_then_recover_to_slots) {
    _meta->fail_slots = true;

    brpc::RedisClusterChannel channel;
    InitChannel(&channel);
    ASSERT_GT(_meta->nodes_calls.load(std::memory_order_relaxed), 0);

    _meta->fail_slots = false;
    const int before_slots = _meta->slots_calls.load(std::memory_order_relaxed);

    const std::string key = FindKeyForNode(0);
    _meta->owner_override[key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key] = "recover-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ("recover-value", resp.reply(0).data());
    ASSERT_GT(_meta->slots_calls.load(std::memory_order_relaxed), before_slots);
}

TEST_F(RedisClusterChannelTest, cluster_slots_empty_host_uses_seed_host) {
    _meta->slots_empty_host = true;
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(1);
    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("set %s host-fallback-value", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ("OK", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, init_accepts_whitespace_in_seed_list) {
    brpc::RedisClusterChannel channel;
    brpc::RedisClusterChannelOptions options;
    options.enable_periodic_refresh = false;
    const std::string seeds = "  " + _meta->endpoint[0] + " ,   " + _meta->endpoint[1] + "  ";
    ASSERT_EQ(0, channel.Init(seeds, &options));
}

TEST_F(RedisClusterChannelTest, init_with_invalid_seed_tokens_should_fail) {
    brpc::RedisClusterChannel channel;
    ASSERT_NE(0, channel.Init(" ,   , "));
}

TEST_F(RedisClusterChannelTest, init_fails_when_cluster_topology_unavailable) {
    _meta->fail_slots = true;
    _meta->fail_nodes = true;

    brpc::RedisClusterChannel channel;
    brpc::RedisClusterChannelOptions options;
    options.enable_periodic_refresh = false;
    ASSERT_NE(0, channel.Init(SeedList(), &options));
}

TEST_F(RedisClusterChannelTest, ping_without_key_uses_any_endpoint) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("ping"));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("PONG", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, wrong_argument_count_commands_return_error_reply) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("mget"));
    ASSERT_TRUE(req.AddCommand("mset only_key"));
    ASSERT_TRUE(req.AddCommand("del"));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(3, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_TRUE(resp.reply(1).is_error());
    ASSERT_TRUE(resp.reply(2).is_error());
}

TEST_F(RedisClusterChannelTest, malformed_redirect_error_is_returned_directly) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(0);
    _meta->forced_error_by_key[key] = "MOVED not_a_slot bad_endpoint";

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_EQ("MOVED not_a_slot bad_endpoint",
              std::string(resp.reply(0).error_message()));
}

TEST_F(RedisClusterChannelTest, cluster_nodes_parser_ignores_migration_tokens) {
    _meta->fail_slots = true;
    std::ostringstream nodes;
    nodes << "node0 " << _meta->endpoint[0]
          << "@17000 master - 0 0 1 connected 0-" << kSplitSlot
          << " [100->-node1]\n";
    nodes << "node1 " << _meta->endpoint[1]
          << "@17001 master - 0 0 1 connected " << (kSplitSlot + 1)
          << "-16383 [100-<-node0]\n";
    _meta->custom_nodes_payload = nodes.str();

    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(1);
    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("set %s from-nodes", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ("OK", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, eval_numkeys_zero_routes_without_slot) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    const butil::StringPiece parts[] = {
        "eval", "return 'ok'", "0", "arg1"
    };
    ASSERT_TRUE(req.AddCommandByComponents(parts, sizeof(parts) / sizeof(parts[0])));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("OK", resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, mset_stops_after_subcommand_error) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    std::vector<std::string> keys0 = FindKeysForNode(0, 2);
    ASSERT_EQ(2u, keys0.size());
    const std::string key_ok = keys0[0];
    const std::string key_tail = keys0[1];
    const std::string key_err = FindKeyForNode(1);
    ASSERT_NE(key_ok, key_err);
    ASSERT_NE(key_tail, key_err);

    _meta->forced_error_by_key[key_err] = "ERR injected set failure";

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("mset %s v0 %s v1 %s v2",
                               key_ok.c_str(),
                               key_err.c_str(),
                               key_tail.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_NE(std::string::npos,
              std::string(resp.reply(0).error_message()).find("injected set failure"));

    {
        brpc::RedisRequest get_req;
        brpc::RedisResponse get_resp;
        brpc::Controller get_cntl;
        ASSERT_TRUE(get_req.AddCommand("get %s", key_ok.c_str()));
        channel.CallMethod(NULL, &get_cntl, &get_req, &get_resp, NULL);
        ASSERT_FALSE(get_cntl.Failed()) << get_cntl.ErrorText();
        ASSERT_TRUE(get_resp.reply(0).is_string());
        ASSERT_EQ("v0", get_resp.reply(0).data());
    }
    {
        brpc::RedisRequest get_req;
        brpc::RedisResponse get_resp;
        brpc::Controller get_cntl;
        ASSERT_TRUE(get_req.AddCommand("get %s", key_tail.c_str()));
        channel.CallMethod(NULL, &get_cntl, &get_req, &get_resp, NULL);
        ASSERT_FALSE(get_cntl.Failed()) << get_cntl.ErrorText();
        ASSERT_TRUE(get_resp.reply(0).is_nil());
    }
}

TEST_F(RedisClusterChannelTest, integer_aggregate_stops_after_subcommand_error) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key0 = FindKeyForNode(0);
    std::vector<std::string> keys1 = FindKeysForNode(1, 2);
    ASSERT_EQ(2u, keys1.size());
    const std::string key_err = keys1[0];
    const std::string key_tail = keys1[1];

    {
        BAIDU_SCOPED_LOCK(_node[0].mutex);
        _node[0].kv[key0] = "v0";
    }
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key_err] = "verr";
        _node[1].kv[key_tail] = "vtail";
    }
    _meta->forced_error_by_key[key_err] = "ERR injected unlink failure";

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("unlink %s %s %s",
                               key0.c_str(), key_err.c_str(), key_tail.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_NE(std::string::npos,
              std::string(resp.reply(0).error_message()).find("injected unlink failure"));

    brpc::RedisRequest get_req;
    brpc::RedisResponse get_resp;
    brpc::Controller get_cntl;
    ASSERT_TRUE(get_req.AddCommand("get %s", key_tail.c_str()));
    channel.CallMethod(NULL, &get_cntl, &get_req, &get_resp, NULL);
    ASSERT_FALSE(get_cntl.Failed()) << get_cntl.ErrorText();
    ASSERT_TRUE(get_resp.reply(0).is_string());
    ASSERT_EQ("vtail", get_resp.reply(0).data());
}

TEST_F(RedisClusterChannelTest, async_concurrent_calls_with_mixed_redirections) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    std::vector<std::string> keys0 = FindKeysForNode(0, 2);
    ASSERT_EQ(2u, keys0.size());
    const std::string ask_key = keys0[0];
    const std::string moved_key = keys0[1];
    const std::string normal_key = FindKeyForNode(1);

    _meta->enable_ask = true;
    _meta->ask_from = 0;
    _meta->ask_to = 1;
    _meta->ask_key = ask_key;
    _meta->owner_override[moved_key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[ask_key] = "ask-v";
        _node[1].kv[moved_key] = "moved-v";
        _node[1].kv[normal_key] = "normal-v";
    }

    const int req_count = 60;
    bthread::CountdownEvent event(req_count);
    std::vector<std::unique_ptr<brpc::RedisRequest> > requests(req_count);
    std::vector<std::unique_ptr<brpc::RedisResponse> > responses(req_count);
    std::vector<std::unique_ptr<brpc::Controller> > controllers(req_count);
    std::vector<std::unique_ptr<Done> > dones(req_count);
    std::vector<std::string> expected(req_count);

    for (int i = 0; i < req_count; ++i) {
        requests[i].reset(new brpc::RedisRequest);
        responses[i].reset(new brpc::RedisResponse);
        controllers[i].reset(new brpc::Controller);
        dones[i].reset(new Done(&event));

        std::string key;
        if (i % 3 == 0) {
            key = ask_key;
            expected[i] = "ask-v";
        } else if (i % 3 == 1) {
            key = moved_key;
            expected[i] = "moved-v";
        } else {
            key = normal_key;
            expected[i] = "normal-v";
        }
        ASSERT_TRUE(requests[i]->AddCommand("get %s", key.c_str()));
        channel.CallMethod(NULL,
                           controllers[i].get(),
                           requests[i].get(),
                           responses[i].get(),
                           dones[i].get());
    }

    event.wait();

    for (int i = 0; i < req_count; ++i) {
        ASSERT_FALSE(controllers[i]->Failed()) << controllers[i]->ErrorText();
        ASSERT_EQ(1, responses[i]->reply_size());
        ASSERT_TRUE(responses[i]->reply(0).is_string());
        ASSERT_EQ(expected[i], responses[i]->reply(0).data());
    }
    ASSERT_GT(_meta->ask_error_calls.load(std::memory_order_relaxed), 0);
    ASSERT_GT(_meta->moved_error_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(RedisClusterChannelTest, hashtag_keys_route_for_multi_key_commands) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string tag = FindHashTagForNode(1);
    const std::string key0 = "k0{" + tag + "}suffix";
    const std::string key1 = "k1{" + tag + "}suffix";

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("mset %s v0 %s v1", key0.c_str(), key1.c_str()));
    ASSERT_TRUE(req.AddCommand("mget %s %s", key0.c_str(), key1.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(2, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("OK", resp.reply(0).data());
    ASSERT_TRUE(resp.reply(1).is_array());
    ASSERT_EQ("v0", resp.reply(1)[0].data());
    ASSERT_EQ("v1", resp.reply(1)[1].data());
    ASSERT_EQ(0, _meta->moved_error_calls.load(std::memory_order_relaxed));
}

TEST_F(RedisClusterChannelTest, missing_key_get_returns_nil_reply) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(0);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_nil());
}

TEST_F(RedisClusterChannelTest, pipeline_with_string_nil_error_and_string) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key_ok = FindKeyForNode(1);
    const std::string key_nil = FindKeyForNode(0);
    std::string key_err;
    for (int i = 0; i < 200000; ++i) {
        std::ostringstream oss;
        oss << "err_key_" << i;
        if (OwnerBySlot(HashSlot(oss.str())) == 1 && oss.str() != key_ok) {
            key_err = oss.str();
            break;
        }
    }
    ASSERT_FALSE(key_err.empty());

    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key_ok] = "ok-value";
    }
    _meta->forced_error_by_key[key_err] = "ERR injected pipeline error";

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key_ok.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", key_nil.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", key_err.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", key_ok.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(4, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_string());
    ASSERT_EQ("ok-value", resp.reply(0).data());
    ASSERT_TRUE(resp.reply(1).is_nil());
    ASSERT_TRUE(resp.reply(2).is_error());
    ASSERT_NE(std::string::npos,
              std::string(resp.reply(2).error_message()).find("injected pipeline error"));
    ASSERT_TRUE(resp.reply(3).is_string());
    ASSERT_EQ("ok-value", resp.reply(3).data());
}

TEST_F(RedisClusterChannelTest, empty_request_should_fail) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_TRUE(cntl.Failed());
    ASSERT_NE(std::string::npos, cntl.ErrorText().find("no redis command"));
}

TEST_F(RedisClusterChannelTest, pipeline_continues_after_command_error_reply) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key_ok = FindKeyForNode(1);
    const std::string key_err = FindKeyForNode(0);
    _meta->forced_error_by_key[key_err] = "ERR injected get failure";
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key_ok] = "ok-value";
    }

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key_err.c_str()));
    ASSERT_TRUE(req.AddCommand("get %s", key_ok.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(2, resp.reply_size());
    ASSERT_TRUE(resp.reply(0).is_error());
    ASSERT_NE(std::string::npos,
              std::string(resp.reply(0).error_message()).find("injected get failure"));
    ASSERT_TRUE(resp.reply(1).is_string());
    ASSERT_EQ("ok-value", resp.reply(1).data());
}

TEST_F(RedisClusterChannelTest, redirect_updates_slot_cache_even_when_refresh_fails) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key = FindKeyForNode(0);
    _meta->owner_override[key] = 1;
    {
        BAIDU_SCOPED_LOCK(_node[1].mutex);
        _node[1].kv[key] = "value-on-node1";
    }
    _meta->fail_slots = true;
    _meta->fail_nodes = true;

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ("value-on-node1", resp.reply(0).data());

    brpc::RedisRequest req2;
    brpc::RedisResponse resp2;
    brpc::Controller cntl2;
    ASSERT_TRUE(req2.AddCommand("get %s", key.c_str()));
    channel.CallMethod(NULL, &cntl2, &req2, &resp2, NULL);
    ASSERT_FALSE(cntl2.Failed()) << cntl2.ErrorText();
    ASSERT_EQ("value-on-node1", resp2.reply(0).data());

    ASSERT_EQ(1, _meta->moved_error_calls.load(std::memory_order_relaxed));
}

TEST_F(RedisClusterChannelTest, periodic_refresh_fallbacks_to_nodes_when_slots_fail) {
    brpc::RedisClusterChannel channel;
    brpc::RedisClusterChannelOptions options;
    options.enable_periodic_refresh = true;
    options.refresh_interval_s = 1;
    options.max_redirect = 5;
    ASSERT_EQ(0, channel.Init(SeedList(), &options));

    _meta->fail_slots = true;
    const int before_nodes = _meta->nodes_calls.load(std::memory_order_relaxed);
    bool nodes_used = false;
    for (int i = 0; i < 30; ++i) {
        if (_meta->nodes_calls.load(std::memory_order_relaxed) > before_nodes) {
            nodes_used = true;
            break;
        }
        bthread_usleep(100000);
    }
    ASSERT_TRUE(nodes_used);
}

TEST_F(RedisClusterChannelTest, async_pipeline_mixed_commands) {
    brpc::RedisClusterChannel channel;
    InitChannel(&channel);

    const std::string key0 = FindKeyForNode(0);
    const std::string key1 = FindKeyForNode(1);

    brpc::RedisRequest req;
    brpc::RedisResponse resp;
    brpc::Controller cntl;
    ASSERT_TRUE(req.AddCommand("set %s p0", key0.c_str()));
    ASSERT_TRUE(req.AddCommand("set %s p1", key1.c_str()));
    ASSERT_TRUE(req.AddCommand("mget %s %s", key0.c_str(), key1.c_str()));
    ASSERT_TRUE(req.AddCommand("del %s %s", key0.c_str(), key1.c_str()));

    bthread::CountdownEvent event(1);
    Done done(&event);
    channel.CallMethod(NULL, &cntl, &req, &resp, &done);
    event.wait();

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(4, resp.reply_size());
    ASSERT_EQ("OK", resp.reply(0).data());
    ASSERT_EQ("OK", resp.reply(1).data());
    ASSERT_TRUE(resp.reply(2).is_array());
    ASSERT_EQ("p0", resp.reply(2)[0].data());
    ASSERT_EQ("p1", resp.reply(2)[1].data());
    ASSERT_TRUE(resp.reply(3).is_integer());
    ASSERT_EQ(2, resp.reply(3).integer());
}

TEST_F(RedisClusterChannelTest, periodic_refresh_updates_topology_in_background) {
    brpc::RedisClusterChannel channel;
    brpc::RedisClusterChannelOptions options;
    options.enable_periodic_refresh = true;
    options.refresh_interval_s = 1;
    options.max_redirect = 5;
    ASSERT_EQ(0, channel.Init(SeedList(), &options));

    const int initial_slots_calls = _meta->slots_calls.load(std::memory_order_relaxed);
    bool refreshed = false;
    for (int i = 0; i < 30; ++i) {
        if (_meta->slots_calls.load(std::memory_order_relaxed) > initial_slots_calls) {
            refreshed = true;
            break;
        }
        bthread_usleep(100000);
    }
    ASSERT_TRUE(refreshed);
}

TEST_F(RedisClusterChannelTest, periodic_refresh_thread_stops_quickly_on_destroy) {
    typedef std::chrono::steady_clock Clock;
    const Clock::time_point begin = Clock::now();
    {
        brpc::RedisClusterChannel channel;
        brpc::RedisClusterChannelOptions options;
        options.enable_periodic_refresh = true;
        options.refresh_interval_s = 30;
        ASSERT_EQ(0, channel.Init(SeedList(), &options));
    }
    const Clock::time_point end = Clock::now();
    const int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    ASSERT_LT(elapsed_ms, 2000);
}

TEST_F(RedisClusterChannelTest, init_with_empty_seed_should_fail) {
    brpc::RedisClusterChannel channel;
    ASSERT_NE(0, channel.Init(""));
}

}  // namespace
