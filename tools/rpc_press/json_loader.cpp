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
#include <butil/logging.h>
#include <json2pb/pb_to_json.h>
#include <json2pb/json_to_pb.h>
#include "pb_util.h"
#include "json_loader.h"
#include <errno.h>

namespace brpc {

class JsonLoader::Reader {
public:
    explicit Reader(int fd2)
        : _fd(fd2)
        , _brace_depth(0)
        , _quoted(false)
        , _quote_char(0)
    {}

    explicit Reader(const std::string& jsons)
        : _fd(-1)
        , _brace_depth(0)
        , _quoted(false)
        , _quote_char(0) {
        _file_buf.append(jsons);
    }

    bool get_next_json(butil::IOBuf* json1);
    bool read_some();

private:
    int _fd;
    int _brace_depth;
    bool _quoted; // quoted by " or '
    char _quote_char;
    butil::IOPortal _file_buf;
};

// Load data from the file.
bool JsonLoader::Reader::read_some() {
    if (_fd < 0) {  // loading from a string.
        return false;
    }
    ssize_t nr = _file_buf.append_from_file_descriptor(_fd, 65536);
    if (nr < 0) {
        if (errno == EINTR) {
            return read_some();
        }
        PLOG(ERROR) << "Fail to read fd=" << _fd;
        return false;
    } else if (nr == 0) {
        return false;
    } else {
        return true;
    }
}

// Ignore json only with spaces and newline
static bool possibly_valid_json(const butil::IOBuf& json) {
    butil::IOBufAsZeroCopyInputStream it(json);
    const void* data = NULL;
    int size = 0;
    int total_size = 0;
    for (; it.Next(&data, &size); total_size += size) {
        for (int i = 0; i < size; ++i) {
            char c = ((const char*)data)[i];
            if (!isspace(c) && c != '\n') {
                return true;
            }
        }
    }
    return false;
}

// Separate jsons with closed braces.
bool JsonLoader::Reader::get_next_json(butil::IOBuf* json1) {
    if (_file_buf.empty()) {
        if (!read_some()) {
            return false;
        }
    }
    json1->clear();
    while (1) {
        butil::IOBufAsZeroCopyInputStream it(_file_buf);
        const void* data = NULL;
        int size = 0;
        int total_size = 0;
        int skipped = 0;
        for (; it.Next(&data, &size); total_size += size) {
            for (int i = 0; i < size; ++i) {
                const char c = ((const char*)data)[i];
                if (_brace_depth == 0) {
                    // skip any character until the first left brace is found.
                    if (c != '{') {
                        ++skipped;
                        continue;
                    }
                }
                switch (c) {
                case '{':
                    if (!_quoted) {
                        ++_brace_depth;
                    } else {
                        VLOG(1) << "Quoted left brace";
                    }                        
                    break;
                case '}':
                    if (!_quoted) {
                        --_brace_depth;
                        if (_brace_depth == 0) {
                            // the braces are closed, a complete object.
                            _file_buf.cutn(json1, total_size + i + 1);
                            json1->pop_front(skipped);
                            return possibly_valid_json(*json1);
                        } else if (_brace_depth < 0) {
                            LOG(ERROR) << "More right braces than left braces";
                            return false;
                        }
                    } else {
                        VLOG(1) << "Quoted right brace";
                    }
                    break;
                case '"':
                    if (_quoted) {
                        if (_quote_char == '"') {
                            _quoted = false;
                        }
                    } else {
                        _quoted = true;
                        _quote_char = '"';
                    }
                    break;
                case '\'':
                    if (_quoted) {
                        if (_quote_char == '\'') {
                            _quoted = false;
                        }
                    } else {
                        _quoted = true;
                        _quote_char = '\'';
                    }
                    break;
                default:
                    break;
                    // just skip
                }
            }
        }
        if (!_file_buf.empty()) {
            json1->append(_file_buf);
            _file_buf.clear();
        }
        if (!read_some()) {
            json1->pop_front(skipped);
            if (!json1->empty()) {
                return possibly_valid_json(*json1);
            }
            return false;
        }
    }
    return false;
}

JsonLoader::JsonLoader(google::protobuf::compiler::Importer* importer, 
           google::protobuf::DynamicMessageFactory* factory,
           const std::string& service_name,
           const std::string& method_name)
    : _importer(importer)
    , _factory(factory)
    , _service_name(service_name)
    , _method_name(method_name)
{
    _request_prototype = pbrpcframework::get_prototype_by_name(
        _service_name, _method_name, true, _importer, _factory);
}

void JsonLoader::load_messages(
    JsonLoader::Reader* ctx,
    std::deque<google::protobuf::Message*>* out_msgs) {
    out_msgs->clear();
    butil::IOBuf request_json;
    while (ctx->get_next_json(&request_json)) {
        VLOG(1) << "Load " << out_msgs->size() + 1 << "-th json=`"
                << request_json << '\'';
        std::string error;
        google::protobuf::Message* request = _request_prototype->New();
        butil::IOBufAsZeroCopyInputStream wrapper(request_json);
        if (!json2pb::JsonToProtoMessage(&wrapper, request, &error)) {
            LOG(WARNING) << "Fail to convert to pb: " << error << ", json=`"
                         << request_json << '\'';
            delete request;
            continue;
        }
        out_msgs->push_back(request);
        LOG_IF(INFO, (out_msgs->size() % 10000) == 0)
            << "Loaded " << out_msgs->size() << " jsons";
    }
}

void JsonLoader::load_messages(
    int fd,
    std::deque<google::protobuf::Message*>* out_msgs) {
    JsonLoader::Reader ctx(fd);
    load_messages(&ctx, out_msgs);
}

void JsonLoader::load_messages(
    const std::string& jsons,
    std::deque<google::protobuf::Message*>* out_msgs) {
    JsonLoader::Reader ctx(jsons);
    load_messages(&ctx, out_msgs);
}

} // namespace brpc
