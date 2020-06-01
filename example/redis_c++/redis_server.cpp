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
//
// A brpc based redis-server. Currently just implement set and
// get, but it's sufficient that you can get the idea how to
// implement brpc::RedisCommandHandler.

#include <brpc/server.h>
#include <brpc/redis.h>
#include <butil/crc32c.h>
#include <butil/strings/string_split.h>
#include <gflags/gflags.h>
#include <unordered_map>

#include <butil/time.h>

DEFINE_int32(port, 6379, "TCP Port of this server");

class RedisServiceImpl : public brpc::RedisService {
public:
    bool Set(const std::string& key, const std::string& value) {
        int slot = butil::crc32c::Value(key.c_str(), key.size()) % kHashSlotNum;
        _mutex[slot].lock();
        _map[slot][key] = value;
        _mutex[slot].unlock();
        return true;
    }

    bool Get(const std::string& key, std::string* value) {
        int slot = butil::crc32c::Value(key.c_str(), key.size()) % kHashSlotNum;
        _mutex[slot].lock();
        auto it = _map[slot].find(key);
        if (it == _map[slot].end()) {
            _mutex[slot].unlock();
            return false;
        }
        *value = it->second;
        _mutex[slot].unlock();
        return true;
    }

private:
    const static int kHashSlotNum = 32;
    std::unordered_map<std::string, std::string> _map[kHashSlotNum];
    butil::Mutex _mutex[kHashSlotNum];
};

class GetCommandHandler : public brpc::RedisCommandHandler {
public:
    explicit GetCommandHandler(RedisServiceImpl* rsimpl)
        : _rsimpl(rsimpl) {}

    brpc::RedisCommandHandlerResult Run(const std::vector<butil::StringPiece>& args,
                                        brpc::RedisReply* output,
                                        bool /*flush_batched*/) override {
        if (args.size() != 2ul) {
            output->FormatError("Expect 1 arg for 'get', actually %lu", args.size()-1);
            return brpc::REDIS_CMD_HANDLED;
        }
        const std::string key(args[1].data(), args[1].size());
        std::string value;
        if (_rsimpl->Get(key, &value)) {
            output->SetString(value);
        } else {
            output->SetNullString();
        }
        return brpc::REDIS_CMD_HANDLED;
	}

private:
   	RedisServiceImpl* _rsimpl;
};

class SetCommandHandler : public brpc::RedisCommandHandler {
public:
    explicit SetCommandHandler(RedisServiceImpl* rsimpl)
        : _rsimpl(rsimpl) {}

    brpc::RedisCommandHandlerResult Run(const std::vector<butil::StringPiece>& args,
                                        brpc::RedisReply* output,
                                        bool /*flush_batched*/) override {
        if (args.size() != 3ul) {
            output->FormatError("Expect 2 args for 'set', actually %lu", args.size()-1);
            return brpc::REDIS_CMD_HANDLED;
        }
        const std::string key(args[1].data(), args[1].size());
        const std::string value(args[2].data(), args[2].size());
        _rsimpl->Set(key, value);
        output->SetStatus("OK");
        return brpc::REDIS_CMD_HANDLED;
	}

private:
   	RedisServiceImpl* _rsimpl;
};

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    RedisServiceImpl* rsimpl = new RedisServiceImpl;
    rsimpl->AddCommandHandler("get", new GetCommandHandler(rsimpl));
    rsimpl->AddCommandHandler("set", new SetCommandHandler(rsimpl));

    brpc::Server server;
    brpc::ServerOptions server_options;
    server_options.redis_service = rsimpl;
    if (server.Start(FLAGS_port, &server_options) != 0) {
        LOG(ERROR) << "Fail to start server";
        return -1;
    }
    server.RunUntilAskedToQuit();
    return 0;
}
