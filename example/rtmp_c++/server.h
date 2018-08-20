// Copyright (c) 2014 Baidu, Inc.
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

// A server to rtmp publish and send rtmp play.

#include <map>
#include <set>
#include <vector>

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/rtmp.h>
#include <brpc/policy/rtmp_protocol.h>

namespace example {

enum RtmpRole {
    RTMP_ROLE_UNKNOWN = -1,
    RTMP_ROLE_PUBLISH,
    RTMP_ROLE_PLAY,
};

class RtmpServiceImpl;

class SimpleRtmpStream : public brpc::RtmpServerStream
{
public:
    explicit SimpleRtmpStream(RtmpServiceImpl* rtmp_service_impl);
    virtual ~SimpleRtmpStream();

    void OnStop();
	void OnPublish(const std::string& stream_name,
                   brpc::RtmpPublishType publish_type,
                   butil::Status* status,
                   google::protobuf::Closure* done);
	void OnPlay(const brpc::RtmpPlayOptions& opt,
                butil::Status* status,
                google::protobuf::Closure* done);
    void OnMetaData(brpc::RtmpMetaData* metadata, const butil::StringPiece& name);
    void OnAudioMessage(brpc::RtmpAudioMessage* msg);
    void OnVideoMessage(brpc::RtmpVideoMessage* msg);

private:
    brpc::RtmpVideoMessage _video_header;
    brpc::RtmpAudioMessage _audio_header;
    brpc::RtmpMetaData _metadata;

    std::vector<brpc::RtmpVideoMessage> _cacheVideo;
    std::vector<brpc::RtmpAudioMessage> _cacheAudio;

    RtmpServiceImpl* _service_impl;

    std::string _stream_name;
    RtmpRole _role;
};

class RtmpServiceImpl : public brpc::RtmpService {
public:
    brpc::RtmpServerStream* NewStream(const brpc::RtmpConnectRequest&);
    bool AddPublishStream(const std::string& stream_name, 
                          brpc::RtmpServerStream* stream);

    void DelPublishStream(const std::string& stream_name);

    brpc::RtmpServerStream* FindPublishStream(const std::string& stream_name);

    bool AddPlayStream(const std::string& stream_name, 
                       brpc::RtmpServerStream* stream);

    void DelPlayStream(const std::string& stream_name, brpc::RtmpServerStream* stream);

    std::set<brpc::RtmpServerStream*> GetPlayStream(const std::string& stream_name);

private:
    std::map<std::string, brpc::RtmpServerStream*> _publish_stream;
    std::map<std::string, std::set<brpc::RtmpServerStream*> > _play_stream;
};

}  // namespace example
