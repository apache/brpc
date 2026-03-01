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

#include "brpc/redis_cluster.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include <algorithm>
#include <set>
#include <sstream>

#include "butil/endpoint.h"
#include "butil/logging.h"
#include "brpc/controller.h"
#include "brpc/redis_command.h"

namespace brpc {
namespace {

static const size_t kRedisClusterSlotCount = 16384;

static const uint16_t kCrc16Table[256] = {
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

static std::string Trim(const std::string& in) {
    size_t begin = 0;
    while (begin < in.size() && isspace(static_cast<unsigned char>(in[begin]))) {
        ++begin;
    }
    size_t end = in.size();
    while (end > begin && isspace(static_cast<unsigned char>(in[end - 1]))) {
        --end;
    }
    return in.substr(begin, end - begin);
}

static std::vector<std::string> SplitByChar(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::string current;
    std::stringstream ss(text);
    while (std::getline(ss, current, delim)) {
        current = Trim(current);
        if (!current.empty()) {
            out.push_back(current);
        }
    }
    return out;
}

static std::vector<std::string> SplitByWhitespace(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string token;
    while (ss >> token) {
        out.push_back(token);
    }
    return out;
}

static std::string EndpointHost(const std::string& endpoint) {
    std::string ep = endpoint;
    if (!ep.empty() && ep[0] == '[') {
        const size_t right = ep.find(']');
        if (right != std::string::npos) {
            return ep.substr(1, right - 1);
        }
        return ep;
    }
    const size_t p = ep.rfind(':');
    if (p == std::string::npos) {
        return ep;
    }
    return ep.substr(0, p);
}

static bool EncodeReply(const RedisReply& reply, butil::IOBuf* out) {
    butil::IOBufAppender appender;
    // RedisReply::SerializeTo does not support REDIS_REPLY_NIL directly.
    // Encode nil as a null bulk string so response parsing can consume it.
    if (reply.type() == REDIS_REPLY_NIL) {
        appender.append("$-1\r\n", 5);
        appender.move_to(*out);
        return true;
    }
    if (!const_cast<RedisReply&>(reply).SerializeTo(&appender)) {
        return false;
    }
    appender.move_to(*out);
    return true;
}

}  // namespace

RedisClusterChannelOptions::RedisClusterChannelOptions()
    : max_redirect(5)
    , refresh_interval_s(30)
    , enable_periodic_refresh(true)
    , topology_refresh_timeout_ms(1000) {
    channel_options.protocol = brpc::PROTOCOL_REDIS;
}

RedisClusterChannel::SingleCommandResult::SingleCommandResult()
    : ok(false)
    , is_status_ok(false)
    , integer_value(0)
    , is_error(false) {
}

struct RedisClusterChannel::AsyncCall {
    RedisClusterChannel* self;
    Controller* cntl;
    const RedisRequest* request;
    RedisResponse* response;
    google::protobuf::Closure* done;
};

RedisClusterChannel::RedisClusterChannel()
    : _stop_refresh(false)
    , _refresh_started(false)
    , _refresh_tid(0) {
    _db_slot_to_endpoint.Modify([](std::vector<std::string>& bg) -> size_t {
        bg.assign(kRedisClusterSlotCount, std::string());
        return 1;
    });
}

RedisClusterChannel::~RedisClusterChannel() {
    _stop_refresh.store(true);
    if (_refresh_started) {
        bthread_join(_refresh_tid, NULL);
    }
}

int RedisClusterChannel::Init(const std::string& seed_nodes,
                              const RedisClusterChannelOptions* options) {
    if (seed_nodes.empty()) {
        LOG(ERROR) << "seed_nodes is empty";
        return -1;
    }

    RedisClusterChannelOptions resolved;
    if (options) {
        resolved = *options;
    }
    resolved.channel_options.protocol = brpc::PROTOCOL_REDIS;
    _options = resolved;

    const std::vector<std::string> seeds = SplitByChar(seed_nodes, ',');
    if (seeds.empty()) {
        LOG(ERROR) << "No valid seed endpoint in " << seed_nodes;
        return -1;
    }

    {
        BAIDU_SCOPED_LOCK(_mutex);
        _seed_endpoints = seeds;
    }

    for (size_t i = 0; i < seeds.size(); ++i) {
        if (GetOrCreateChannel(seeds[i]) == NULL) {
            LOG(WARNING) << "Fail to init seed channel=" << seeds[i];
        }
    }

    if (!RefreshTopology()) {
        LOG(ERROR) << "Fail to fetch redis cluster topology from seeds";
        return -1;
    }

    if (_options.enable_periodic_refresh && _options.refresh_interval_s > 0) {
        _stop_refresh.store(false);
        if (bthread_start_background(&_refresh_tid, NULL,
                                     RedisClusterChannel::RunPeriodicRefresh,
                                     this) == 0) {
            _refresh_started = true;
        } else {
            LOG(WARNING) << "Fail to start periodic refresh bthread";
        }
    }
    return 0;
}

void RedisClusterChannel::CallMethod(
    const google::protobuf::MethodDescriptor* /*method*/,
    google::protobuf::RpcController* controller_base,
    const google::protobuf::Message* request_base,
    google::protobuf::Message* response_base,
    google::protobuf::Closure* done) {
    Controller* cntl = static_cast<Controller*>(controller_base);
    if (cntl == NULL) {
        LOG(ERROR) << "controller is NULL";
        if (done) {
            done->Run();
        }
        return;
    }

    if (request_base == NULL ||
        request_base->GetDescriptor() != RedisRequest::descriptor()) {
        cntl->SetFailed(EREQUEST, "request must be RedisRequest");
        if (done) {
            done->Run();
        }
        return;
    }
    if (response_base == NULL ||
        response_base->GetDescriptor() != RedisResponse::descriptor()) {
        cntl->SetFailed(ERESPONSE, "response must be RedisResponse");
        if (done) {
            done->Run();
        }
        return;
    }

    const RedisRequest* request = static_cast<const RedisRequest*>(request_base);
    RedisResponse* response = static_cast<RedisResponse*>(response_base);

    if (done == NULL) {
        CallMethodImpl(cntl, *request, response);
        return;
    }

    AsyncCall* ac = new (std::nothrow) AsyncCall;
    if (ac == NULL) {
        cntl->SetFailed(ENOMEM, "Fail to allocate async context");
        done->Run();
        return;
    }
    ac->self = this;
    ac->cntl = cntl;
    ac->request = request;
    ac->response = response;
    ac->done = done;

    bthread_t tid;
    if (bthread_start_background(&tid, NULL, RedisClusterChannel::RunAsyncCall, ac) != 0) {
        delete ac;
        CallMethodImpl(cntl, *request, response);
        done->Run();
    }
}

bool RedisClusterChannel::CallMethodImpl(Controller* cntl,
                                         const RedisRequest& request,
                                         RedisResponse* response) {
    std::vector<ParsedCommand> commands;
    if (!ParseRequest(request, &commands, cntl)) {
        return false;
    }
    if (commands.empty()) {
        cntl->SetFailed(EREQUEST, "request has no redis command");
        return false;
    }

    std::vector<butil::IOBuf> replies(commands.size());
    for (size_t i = 0; i < commands.size(); ++i) {
        if (!ExecuteCommand(commands[i], &replies[i], cntl)) {
            return false;
        }
    }

    butil::IOBuf merged;
    for (size_t i = 0; i < replies.size(); ++i) {
        merged.append(replies[i]);
    }

    response->Clear();
    ParseError err = response->ConsumePartialIOBuf(merged, static_cast<int>(commands.size()));
    if (err != PARSE_OK || !merged.empty()) {
        cntl->SetFailed(ERESPONSE, "Fail to parse merged redis response");
        return false;
    }
    return true;
}

bool RedisClusterChannel::ParseRequest(const RedisRequest& request,
                                       std::vector<ParsedCommand>* commands,
                                       Controller* cntl) const {
    commands->clear();

    butil::IOBuf serialized;
    if (!request.SerializeTo(&serialized)) {
        cntl->SetFailed(EREQUEST, "Fail to serialize redis request");
        return false;
    }

    RedisCommandParser parser;
    butil::Arena arena;

    while (!serialized.empty()) {
        std::vector<butil::StringPiece> args;
        ParseError err = parser.Consume(serialized, &args, &arena);
        if (err != PARSE_OK) {
            cntl->SetFailed(EREQUEST, "Fail to parse redis request (err=%d)", err);
            return false;
        }
        ParsedCommand cmd;
        cmd.args.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            cmd.args.push_back(args[i].as_string());
        }
        commands->push_back(cmd);
    }
    return true;
}

bool RedisClusterChannel::ExecuteCommand(const ParsedCommand& cmd,
                                         butil::IOBuf* encoded_reply,
                                         Controller* cntl) {
    if (cmd.args.empty()) {
        cntl->SetFailed(EREQUEST, "Empty redis command");
        return false;
    }
    const std::string& name = cmd.args[0];

    if (name == "multi" || name == "exec") {
        AppendErrorReply(encoded_reply,
                         "ERR MULTI/EXEC is not supported by RedisClusterChannel");
        return true;
    }
    if (name == "mget") {
        return ExecuteMGet(cmd, encoded_reply, cntl);
    }
    if (name == "mset") {
        return ExecuteMSet(cmd, encoded_reply, cntl);
    }
    if (name == "del" || name == "exists" || name == "unlink") {
        return ExecuteIntegerAggregate(cmd, encoded_reply, cntl);
    }
    if (name == "eval" || name == "evalsha") {
        return ExecuteEvalLike(cmd, encoded_reply, cntl);
    }

    SingleCommandResult result;
    if (!ExecuteSingleCommand(cmd.args, NULL, &result, cntl)) {
        return false;
    }
    encoded_reply->swap(result.encoded_reply);
    return true;
}

bool RedisClusterChannel::ExecuteSingleCommand(const std::vector<std::string>& args,
                                               const std::string* forced_endpoint,
                                               SingleCommandResult* result,
                                               Controller* cntl) {
    if (args.empty()) {
        cntl->SetFailed(EREQUEST, "Empty redis command");
        return false;
    }

    std::string endpoint;
    int key_slot = -1;
    if (forced_endpoint != NULL) {
        endpoint = *forced_endpoint;
    } else if (!IsNoKeyCommand(args[0]) && args.size() >= 2) {
        if (!PickEndpointForKey(args[1], &endpoint, &key_slot)) {
            RefreshTopology();
            if (!PickEndpointForKey(args[1], &endpoint, &key_slot)) {
                cntl->SetFailed(EHOSTDOWN, "No endpoint found for key");
                return false;
            }
        }
    } else {
        if (!PickAnyEndpoint(&endpoint)) {
            RefreshTopology();
            if (!PickAnyEndpoint(&endpoint)) {
                cntl->SetFailed(EHOSTDOWN, "No endpoint available in redis cluster");
                return false;
            }
        }
    }

    bool asking = false;
    std::string next_endpoint = endpoint;
    const int max_redirect = std::max(_options.max_redirect, 0);
    for (int i = 0; i <= max_redirect; ++i) {
        RedirectInfo redirect;
        if (!SendToEndpoint(next_endpoint, args, asking, result, &redirect, cntl)) {
            return false;
        }
        if (!redirect.valid) {
            return true;
        }

        if (!redirect.endpoint.empty()) {
            next_endpoint = redirect.endpoint;
            GetOrCreateChannel(next_endpoint);
        }
        // ASK is a temporary redirection during slot migration and should not
        // overwrite the stable slot map. Only persist MOVED target.
        if (!redirect.asking &&
            redirect.slot >= 0 &&
            redirect.slot < static_cast<int>(kRedisClusterSlotCount) &&
            !redirect.endpoint.empty()) {
            _db_slot_to_endpoint.Modify(
                [](std::vector<std::string>& bg, int slot,
                   const std::string& endpoint) -> size_t {
                    if (bg[slot] == endpoint) {
                        return 0;
                    }
                    bg[slot] = endpoint;
                    return 1;
                },
                redirect.slot, redirect.endpoint);
        }

        if (!redirect.asking) {
            RefreshTopology();
        }
        asking = redirect.asking;
    }

    cntl->SetFailed(ERESPONSE, "Too many redis cluster redirects");
    return false;
}

bool RedisClusterChannel::ExecuteMGet(const ParsedCommand& cmd,
                                      butil::IOBuf* encoded_reply,
                                      Controller* cntl) {
    if (cmd.args.size() < 2) {
        AppendErrorReply(encoded_reply,
                         "ERR wrong number of arguments for 'mget' command");
        return true;
    }

    std::vector<butil::IOBuf> values;
    values.reserve(cmd.args.size() - 1);
    for (size_t i = 1; i < cmd.args.size(); ++i) {
        std::vector<std::string> sub_args;
        sub_args.push_back("get");
        sub_args.push_back(cmd.args[i]);
        SingleCommandResult sub_result;
        if (!ExecuteSingleCommand(sub_args, NULL, &sub_result, cntl)) {
            return false;
        }
        values.push_back(sub_result.encoded_reply);
    }

    AppendArrayHeader(encoded_reply, values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        encoded_reply->append(values[i]);
    }
    return true;
}

bool RedisClusterChannel::ExecuteMSet(const ParsedCommand& cmd,
                                      butil::IOBuf* encoded_reply,
                                      Controller* cntl) {
    if (cmd.args.size() < 3 || ((cmd.args.size() - 1) % 2 != 0)) {
        AppendErrorReply(encoded_reply,
                         "ERR wrong number of arguments for 'mset' command");
        return true;
    }

    for (size_t i = 1; i + 1 < cmd.args.size(); i += 2) {
        std::vector<std::string> sub_args;
        sub_args.push_back("set");
        sub_args.push_back(cmd.args[i]);
        sub_args.push_back(cmd.args[i + 1]);

        SingleCommandResult sub_result;
        if (!ExecuteSingleCommand(sub_args, NULL, &sub_result, cntl)) {
            return false;
        }
        if (sub_result.is_error || !sub_result.is_status_ok) {
            encoded_reply->swap(sub_result.encoded_reply);
            return true;
        }
    }

    AppendStatusReply(encoded_reply, "OK");
    return true;
}

bool RedisClusterChannel::ExecuteIntegerAggregate(const ParsedCommand& cmd,
                                                  butil::IOBuf* encoded_reply,
                                                  Controller* cntl) {
    if (cmd.args.size() < 2) {
        AppendErrorReply(encoded_reply,
                         "ERR wrong number of arguments");
        return true;
    }

    int64_t total = 0;
    for (size_t i = 1; i < cmd.args.size(); ++i) {
        std::vector<std::string> sub_args;
        sub_args.push_back(cmd.args[0]);
        sub_args.push_back(cmd.args[i]);

        SingleCommandResult sub_result;
        if (!ExecuteSingleCommand(sub_args, NULL, &sub_result, cntl)) {
            return false;
        }
        if (sub_result.is_error) {
            encoded_reply->swap(sub_result.encoded_reply);
            return true;
        }
        total += sub_result.integer_value;
    }

    AppendIntegerReply(encoded_reply, total);
    return true;
}

bool RedisClusterChannel::ExecuteEvalLike(const ParsedCommand& cmd,
                                          butil::IOBuf* encoded_reply,
                                          Controller* cntl) {
    if (cmd.args.size() < 3) {
        AppendErrorReply(encoded_reply,
                         "ERR wrong number of arguments for eval/evalsha");
        return true;
    }

    int64_t numkeys = 0;
    if (!ParseInt(cmd.args[2], &numkeys) || numkeys < 0) {
        AppendErrorReply(encoded_reply,
                         "ERR invalid numkeys for eval/evalsha");
        return true;
    }

    if (cmd.args.size() < static_cast<size_t>(3 + numkeys)) {
        AppendErrorReply(encoded_reply,
                         "ERR not enough keys for eval/evalsha");
        return true;
    }

    std::string forced_endpoint;
    if (numkeys > 0) {
        const std::string tag_key = cmd.args[3];
        const int first_slot = HashSlot(tag_key);
        for (int64_t i = 1; i < numkeys; ++i) {
            if (HashSlot(cmd.args[3 + i]) != first_slot) {
                AppendErrorReply(encoded_reply,
                                 "CROSSSLOT Keys in request don't hash to the same slot");
                return true;
            }
        }

        int slot = -1;
        if (!PickEndpointForKey(tag_key, &forced_endpoint, &slot)) {
            RefreshTopology();
            if (!PickEndpointForKey(tag_key, &forced_endpoint, &slot)) {
                cntl->SetFailed(EHOSTDOWN, "No endpoint found for eval/evalsha");
                return false;
            }
        }
    }

    SingleCommandResult result;
    if (!ExecuteSingleCommand(cmd.args,
                              numkeys > 0 ? &forced_endpoint : NULL,
                              &result,
                              cntl)) {
        return false;
    }
    encoded_reply->swap(result.encoded_reply);
    return true;
}

bool RedisClusterChannel::PickEndpointForKey(const std::string& key,
                                             std::string* endpoint,
                                             int* slot) const {
    const int key_slot = HashSlot(key);
    if (key_slot < 0 || key_slot >= static_cast<int>(kRedisClusterSlotCount)) {
        return false;
    }
    butil::DoublyBufferedData<std::vector<std::string> >::ScopedPtr s;
    if (_db_slot_to_endpoint.Read(&s) != 0 ||
        static_cast<size_t>(key_slot) >= s->size()) {
        return false;
    }
    const std::string& mapped = (*s)[key_slot];
    if (mapped.empty()) {
        return false;
    }
    *endpoint = mapped;
    if (slot != NULL) {
        *slot = key_slot;
    }
    return true;
}

bool RedisClusterChannel::PickAnyEndpoint(std::string* endpoint) const {
    BAIDU_SCOPED_LOCK(_mutex);
    for (std::unordered_map<std::string, std::unique_ptr<Channel> >::const_iterator
             it = _channels.begin(); it != _channels.end(); ++it) {
        *endpoint = it->first;
        return true;
    }
    if (!_seed_endpoints.empty()) {
        *endpoint = _seed_endpoints.front();
        return true;
    }
    return false;
}

bool RedisClusterChannel::SendToEndpoint(const std::string& endpoint,
                                         const std::vector<std::string>& args,
                                         bool asking,
                                         SingleCommandResult* result,
                                         RedirectInfo* redirect,
                                         Controller* cntl) {
    result->ok = false;
    redirect->valid = false;
    redirect->asking = false;
    redirect->slot = -1;
    redirect->endpoint.clear();

    Channel* channel = GetOrCreateChannel(endpoint);
    if (channel == NULL) {
        cntl->SetFailed(EHOSTDOWN, "Fail to get channel for %s", endpoint.c_str());
        return false;
    }

    RedisRequest request;
    if (asking) {
        if (!request.AddCommand("asking")) {
            cntl->SetFailed(EREQUEST, "Fail to build ASKING command");
            return false;
        }
        std::vector<butil::StringPiece> components;
        components.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            components.push_back(args[i]);
        }
        if (!request.AddCommandByComponents(&components[0], components.size())) {
            cntl->SetFailed(EREQUEST, "Fail to build redis command");
            return false;
        }
    } else {
        if (!BuildRedisRequest(args, &request)) {
            cntl->SetFailed(EREQUEST, "Fail to build redis command");
            return false;
        }
    }

    RedisResponse response;
    Controller sub_cntl;
    if (cntl->timeout_ms() > 0) {
        sub_cntl.set_timeout_ms(cntl->timeout_ms());
    }
    channel->CallMethod(NULL, &sub_cntl, &request, &response, NULL);
    if (sub_cntl.Failed()) {
        cntl->SetFailed(sub_cntl.ErrorCode(),
                        "Redis cluster sub-request to %s failed: %s",
                        endpoint.c_str(),
                        sub_cntl.ErrorText().c_str());
        return false;
    }

    const int expected = asking ? 2 : 1;
    if (response.reply_size() != expected) {
        cntl->SetFailed(ERESPONSE,
                        "Unexpected redis response size=%d expected=%d",
                        response.reply_size(),
                        expected);
        return false;
    }

    const RedisReply& selected = response.reply(asking ? 1 : 0);
    if (!EncodeReply(selected, &result->encoded_reply)) {
        cntl->SetFailed(ERESPONSE, "Fail to encode redis reply");
        return false;
    }

    result->ok = true;
    result->is_error = selected.is_error();
    result->is_status_ok = (selected.type() == REDIS_REPLY_STATUS &&
                            selected.data() == "OK");
    result->integer_value = selected.is_integer() ? selected.integer() : 0;
    if (result->is_error) {
        result->error_text = selected.error_message();
    } else {
        result->error_text.clear();
    }

    ParseRedirectReply(*result, redirect);
    return true;
}

bool RedisClusterChannel::RefreshTopology() {
    std::vector<std::string> candidates;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        candidates = _seed_endpoints;
        for (std::unordered_map<std::string, std::unique_ptr<Channel> >::const_iterator
                 it = _channels.begin(); it != _channels.end(); ++it) {
            candidates.push_back(it->first);
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (RefreshTopologyFromEndpoint(candidates[i])) {
            return true;
        }
    }
    return false;
}

bool RedisClusterChannel::RefreshTopologyFromEndpoint(const std::string& endpoint) {
    Channel* channel = GetOrCreateChannel(endpoint);
    if (channel == NULL) {
        return false;
    }

    std::vector<std::string> slot_to_endpoint(kRedisClusterSlotCount);
    std::vector<std::string> discovered;
    if (FetchAndParseClusterSlots(channel, endpoint,
                                  &slot_to_endpoint, &discovered)) {
        ApplyTopology(slot_to_endpoint, discovered);
        return true;
    }

    slot_to_endpoint.assign(kRedisClusterSlotCount, std::string());
    discovered.clear();
    if (FetchAndParseClusterNodes(channel, &slot_to_endpoint, &discovered)) {
        ApplyTopology(slot_to_endpoint, discovered);
        return true;
    }
    return false;
}

bool RedisClusterChannel::FetchAndParseClusterSlots(
    Channel* channel,
    const std::string& endpoint,
    std::vector<std::string>* slot_to_endpoint,
    std::vector<std::string>* discovered_endpoints) {
    RedisRequest request;
    if (!request.AddCommand("cluster slots")) {
        return false;
    }

    RedisResponse response;
    Controller cntl;
    cntl.set_timeout_ms(_options.topology_refresh_timeout_ms);
    channel->CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        return false;
    }

    if (response.reply_size() != 1 || !response.reply(0).is_array()) {
        return false;
    }

    const RedisReply& root = response.reply(0);
    const std::string fallback_host = EndpointHost(endpoint);
    bool has_slot = false;
    for (size_t i = 0; i < root.size(); ++i) {
        const RedisReply& item = root[i];
        if (!item.is_array() || item.size() < 3) {
            continue;
        }
        if (!item[0].is_integer() || !item[1].is_integer()) {
            continue;
        }

        int64_t start = item[0].integer();
        int64_t end = item[1].integer();
        if (start < 0 || end < start) {
            continue;
        }

        if (!item[2].is_array() || item[2].size() < 2 || !item[2][1].is_integer()) {
            continue;
        }
        std::string host;
        if (item[2][0].is_string()) {
            host = item[2][0].data().as_string();
        }
        if (host.empty()) {
            host = fallback_host;
        }

        const int64_t port = item[2][1].integer();
        if (port <= 0 || port > 65535) {
            continue;
        }

        std::ostringstream oss;
        oss << host << ":" << port;
        const std::string master_endpoint = oss.str();
        discovered_endpoints->push_back(master_endpoint);

        start = std::max<int64_t>(start, 0);
        end = std::min<int64_t>(end, static_cast<int64_t>(kRedisClusterSlotCount - 1));
        for (int64_t slot = start; slot <= end; ++slot) {
            (*slot_to_endpoint)[slot] = master_endpoint;
            has_slot = true;
        }
    }
    return has_slot;
}

bool RedisClusterChannel::FetchAndParseClusterNodes(
    Channel* channel,
    std::vector<std::string>* slot_to_endpoint,
    std::vector<std::string>* discovered_endpoints) {
    RedisRequest request;
    if (!request.AddCommand("cluster nodes")) {
        return false;
    }

    RedisResponse response;
    Controller cntl;
    cntl.set_timeout_ms(_options.topology_refresh_timeout_ms);
    channel->CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        return false;
    }

    if (response.reply_size() != 1 || !response.reply(0).is_string()) {
        return false;
    }

    bool has_slot = false;
    const std::string payload = response.reply(0).data().as_string();
    const std::vector<std::string> lines = SplitByChar(payload, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }
        const std::vector<std::string> fields = SplitByWhitespace(line);
        if (fields.size() < 8) {
            continue;
        }

        const std::string& flags = fields[2];
        if (flags.find("master") == std::string::npos) {
            continue;
        }
        if (flags.find("fail") != std::string::npos ||
            flags.find("handshake") != std::string::npos ||
            flags.find("noaddr") != std::string::npos) {
            continue;
        }

        std::string endpoint;
        if (!ParseRedisNodeAddress(fields[1], &endpoint)) {
            continue;
        }
        discovered_endpoints->push_back(endpoint);

        for (size_t j = 8; j < fields.size(); ++j) {
            const std::string& slot_token = fields[j];
            if (slot_token.empty() ||
                slot_token[0] == '[' ||
                slot_token.find("->") != std::string::npos ||
                slot_token.find("<-") != std::string::npos) {
                continue;
            }

            size_t dash = slot_token.find('-');
            int64_t start = 0;
            int64_t end = 0;
            if (dash == std::string::npos) {
                if (!ParseInt(slot_token, &start)) {
                    continue;
                }
                end = start;
            } else {
                if (!ParseInt(slot_token.substr(0, dash), &start) ||
                    !ParseInt(slot_token.substr(dash + 1), &end)) {
                    continue;
                }
            }

            if (start < 0 || end < start) {
                continue;
            }
            start = std::max<int64_t>(start, 0);
            end = std::min<int64_t>(end, static_cast<int64_t>(kRedisClusterSlotCount - 1));
            for (int64_t slot = start; slot <= end; ++slot) {
                (*slot_to_endpoint)[slot] = endpoint;
                has_slot = true;
            }
        }
    }
    return has_slot;
}

void RedisClusterChannel::ApplyTopology(
    const std::vector<std::string>& slot_to_endpoint,
    const std::vector<std::string>& discovered_endpoints) {
    std::set<std::string> unique_eps;
    for (size_t i = 0; i < discovered_endpoints.size(); ++i) {
        if (!discovered_endpoints[i].empty()) {
            unique_eps.insert(discovered_endpoints[i]);
        }
    }
    for (size_t i = 0; i < slot_to_endpoint.size(); ++i) {
        if (!slot_to_endpoint[i].empty()) {
            unique_eps.insert(slot_to_endpoint[i]);
        }
    }

    for (std::set<std::string>::const_iterator it = unique_eps.begin();
         it != unique_eps.end(); ++it) {
        GetOrCreateChannel(*it);
    }

    _db_slot_to_endpoint.Modify(
        [](std::vector<std::string>& bg,
           const std::vector<std::string>& src) -> size_t {
            bg = src;
            return 1;
        },
        slot_to_endpoint);

    BAIDU_SCOPED_LOCK(_mutex);
    for (std::set<std::string>::const_iterator it = unique_eps.begin();
         it != unique_eps.end(); ++it) {
        if (std::find(_seed_endpoints.begin(), _seed_endpoints.end(), *it) ==
            _seed_endpoints.end()) {
            _seed_endpoints.push_back(*it);
        }
    }
}

Channel* RedisClusterChannel::GetOrCreateChannel(const std::string& endpoint) {
    {
        BAIDU_SCOPED_LOCK(_mutex);
        std::unordered_map<std::string, std::unique_ptr<Channel> >::iterator it =
            _channels.find(endpoint);
        if (it != _channels.end()) {
            return it->second.get();
        }
    }

    std::unique_ptr<Channel> new_channel(new (std::nothrow) Channel);
    if (!new_channel) {
        return NULL;
    }
    ChannelOptions options = _options.channel_options;
    options.protocol = brpc::PROTOCOL_REDIS;
    if (new_channel->Init(endpoint.c_str(), &options) != 0) {
        return NULL;
    }

    BAIDU_SCOPED_LOCK(_mutex);
    std::unordered_map<std::string, std::unique_ptr<Channel> >::iterator it =
        _channels.find(endpoint);
    if (it != _channels.end()) {
        return it->second.get();
    }
    Channel* ptr = new_channel.get();
    _channels[endpoint] = std::move(new_channel);
    return ptr;
}

uint16_t RedisClusterChannel::HashSlot(const std::string& key) {
    const std::string hashed = ExtractHashtag(key);
    uint16_t crc = 0;
    for (size_t i = 0; i < hashed.size(); ++i) {
        const uint8_t idx = static_cast<uint8_t>((crc >> 8) ^
                          static_cast<uint8_t>(hashed[i]));
        crc = static_cast<uint16_t>((crc << 8) ^ kCrc16Table[idx]);
    }
    return crc & (kRedisClusterSlotCount - 1);
}

std::string RedisClusterChannel::ExtractHashtag(const std::string& key) {
    const size_t begin = key.find('{');
    if (begin == std::string::npos) {
        return key;
    }
    const size_t end = key.find('}', begin + 1);
    if (end == std::string::npos || end == begin + 1) {
        return key;
    }
    return key.substr(begin + 1, end - begin - 1);
}

bool RedisClusterChannel::ParseRedirectReply(const SingleCommandResult& result,
                                             RedirectInfo* redirect) {
    redirect->valid = false;
    redirect->asking = false;
    redirect->slot = -1;
    redirect->endpoint.clear();

    if (!result.is_error || result.error_text.empty()) {
        return false;
    }

    std::vector<std::string> fields = SplitByWhitespace(result.error_text);
    if (fields.size() < 3) {
        return false;
    }

    if (fields[0] == "MOVED") {
        redirect->asking = false;
    } else if (fields[0] == "ASK") {
        redirect->asking = true;
    } else {
        return false;
    }

    int64_t slot = -1;
    if (!ParseInt(fields[1], &slot)) {
        return false;
    }
    std::string endpoint;
    if (!ParseRedisNodeAddress(fields[2], &endpoint)) {
        return false;
    }

    redirect->valid = true;
    redirect->slot = static_cast<int>(slot);
    redirect->endpoint = endpoint;
    return true;
}

bool RedisClusterChannel::BuildRedisRequest(const std::vector<std::string>& args,
                                            RedisRequest* request) {
    request->Clear();
    if (args.empty()) {
        return false;
    }
    std::vector<butil::StringPiece> components;
    components.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        components.push_back(args[i]);
    }
    return request->AddCommandByComponents(&components[0], components.size());
}

void RedisClusterChannel::AppendIntegerReply(butil::IOBuf* buf, int64_t value) {
    std::ostringstream oss;
    oss << ':' << value << "\r\n";
    buf->append(oss.str());
}

void RedisClusterChannel::AppendStatusReply(butil::IOBuf* buf,
                                            const std::string& value) {
    buf->push_back('+');
    buf->append(value);
    buf->append("\r\n");
}

void RedisClusterChannel::AppendErrorReply(butil::IOBuf* buf,
                                           const std::string& value) {
    buf->push_back('-');
    buf->append(value);
    buf->append("\r\n");
}

void RedisClusterChannel::AppendArrayHeader(butil::IOBuf* buf, size_t size) {
    std::ostringstream oss;
    oss << '*' << size << "\r\n";
    buf->append(oss.str());
}

bool RedisClusterChannel::ParseRedisNodeAddress(const std::string& token,
                                                std::string* endpoint) {
    if (token.empty()) {
        return false;
    }
    std::string t = token;

    const size_t comma = t.find(',');
    if (comma != std::string::npos) {
        t.resize(comma);
    }
    const size_t at = t.find('@');
    if (at != std::string::npos) {
        t.resize(at);
    }

    std::string host;
    int64_t port = 0;
    if (!t.empty() && t[0] == '[') {
        const size_t r = t.find(']');
        if (r == std::string::npos || r + 2 > t.size() || t[r + 1] != ':') {
            return false;
        }
        host = t.substr(1, r - 1);
        if (!ParseInt(t.substr(r + 2), &port)) {
            return false;
        }
        std::ostringstream oss;
        oss << '[' << host << "]:" << port;
        *endpoint = oss.str();
        return true;
    }

    const size_t pos = t.rfind(':');
    if (pos == std::string::npos) {
        return false;
    }
    host = t.substr(0, pos);
    if (!ParseInt(t.substr(pos + 1), &port)) {
        return false;
    }
    if (host.empty() || port <= 0 || port > 65535) {
        return false;
    }

    std::ostringstream oss;
    oss << host << ':' << port;
    *endpoint = oss.str();
    return true;
}

bool RedisClusterChannel::ParseInt(const std::string& s, int64_t* out) {
    if (s.empty()) {
        return false;
    }
    char* end = NULL;
    errno = 0;
    const long long value = strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) {
        return false;
    }
    *out = value;
    return true;
}

bool RedisClusterChannel::IsNoKeyCommand(const std::string& cmd) {
    return (cmd == "ping" || cmd == "info" || cmd == "auth" ||
            cmd == "select" || cmd == "echo" || cmd == "time" ||
            cmd == "dbsize" || cmd == "cluster");
}

void* RedisClusterChannel::RunPeriodicRefresh(void* arg) {
    RedisClusterChannel* self = static_cast<RedisClusterChannel*>(arg);
    while (!self->_stop_refresh.load()) {
        const int interval_s = std::max(self->_options.refresh_interval_s, 1);
        int64_t remain_us = interval_s * 1000000L;
        while (remain_us > 0 && !self->_stop_refresh.load()) {
            const int64_t step_us = std::min<int64_t>(remain_us, 100000L);
            bthread_usleep(step_us);
            remain_us -= step_us;
        }
        if (self->_stop_refresh.load()) {
            break;
        }
        self->RefreshTopology();
    }
    return NULL;
}

void* RedisClusterChannel::RunAsyncCall(void* arg) {
    std::unique_ptr<AsyncCall> ac(static_cast<AsyncCall*>(arg));
    ac->self->CallMethodImpl(ac->cntl, *ac->request, ac->response);
    ac->done->Run();
    return NULL;
}

int RedisClusterChannel::CheckHealth() {
    std::string endpoint;
    return PickAnyEndpoint(&endpoint) ? 0 : -1;
}

int RedisClusterChannel::Weight() {
    std::set<std::string> unique;
    butil::DoublyBufferedData<std::vector<std::string> >::ScopedPtr s;
    if (_db_slot_to_endpoint.Read(&s) != 0) {
        return 0;
    }
    for (size_t i = 0; i < s->size(); ++i) {
        if (!(*s)[i].empty()) {
            unique.insert((*s)[i]);
        }
    }
    return static_cast<int>(unique.size());
}

}  // namespace brpc
