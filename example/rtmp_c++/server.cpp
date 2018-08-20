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

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/rtmp.h>
#include <brpc/policy/rtmp_protocol.h>

#include "server.h"

DEFINE_int32(port, 1935, "RTMP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
               "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(logoff_ms, 2000, "Maximum duration of server's LOGOFF state "
             "(waiting for client to close connection before server stops)");

namespace example {

SimpleRtmpStream::SimpleRtmpStream(RtmpServiceImpl* rtmp_service_impl)
    : brpc::RtmpServerStream()
    , _service_impl(rtmp_service_impl)
    , _role(RTMP_ROLE_UNKNOWN) {
}

SimpleRtmpStream::~SimpleRtmpStream() {
    if (_service_impl != NULL) {
        OnStop();
    }
}

void SimpleRtmpStream::OnStop() {
    if (_service_impl == NULL) {
        LOG(ERROR) << "null service impl";
        return;
    }

    if (_role == RTMP_ROLE_PUBLISH) {
        LOG(INFO) << "On publish stream:" << _stream_name << " stop";

        _service_impl->DelPublishStream(_stream_name);
        for (const auto& client : _service_impl->GetPlayStream(_stream_name)) {
            client->SendStopMessage("stop");
        }
    } else if (_role == RTMP_ROLE_PLAY) {
        LOG(INFO) << "On play stream:" << _stream_name << " stop";
        _service_impl->DelPlayStream(_stream_name, this);
    }
    
    // Stop service for this stream.
    _service_impl = NULL;
}

void SimpleRtmpStream::OnPublish(const std::string& stream_name,
                                 brpc::RtmpPublishType publish_type,
                                 butil::Status* status,
                                 google::protobuf::Closure* done) {
    if (stream_name.empty()) {
        LOG(ERROR) << "When publish, stream_name empty";
        status->set_error(EPERM, "%s[%u] publish{stream_name=%s type=%s} stream_name empty",
                          butil::endpoint2str(remote_side()).c_str(), stream_id(),
                          stream_name.c_str(), RtmpPublishType2Str(publish_type));
        return;
    }

    if (_service_impl == NULL) {
        LOG(ERROR) << "null service impl";
        status->set_error(EPERM, "%s[%u] publish{stream_name=%s type=%s} null service impl",
                          butil::endpoint2str(remote_side()).c_str(), stream_id(),
                          stream_name.c_str(), RtmpPublishType2Str(publish_type));
        return;
    }

    if (_service_impl->FindPublishStream(stream_name) != NULL) {
        status->set_error(EPERM, "%s[%u] publish{stream_name=%s type=%s} stream_name already exist",
                          butil::endpoint2str(remote_side()).c_str(), stream_id(),
                          stream_name.c_str(), RtmpPublishType2Str(publish_type));

        LOG(ERROR) << "When publish, stream_name:" << stream_name 
                   << " already exist";
        return;
    }

	done->Run();

    _stream_name = stream_name;
    _role = RTMP_ROLE_PUBLISH;

    _service_impl->AddPublishStream(stream_name, this);
}

void SimpleRtmpStream::OnPlay(const brpc::RtmpPlayOptions& opt,
                              butil::Status* status,
                              google::protobuf::Closure* done) {
    if (_service_impl == NULL) {
        LOG(ERROR) << "null service impl";

		status->set_error(EPERM, "%s[%u] play{stream_name=%s start=%f"
                          " duration=%f reset=%d} null service impl",
                          butil::endpoint2str(remote_side()).c_str(), stream_id(),
                          opt.stream_name.c_str(), opt.start, opt.duration,
                          (int)opt.reset);
        return;
    }

    SimpleRtmpStream* publish_stream = 
        static_cast<SimpleRtmpStream*>(_service_impl->FindPublishStream(opt.stream_name));

    if (publish_stream == NULL) {
		status->set_error(EPERM, "%s[%u] play{stream_name=%s start=%f"
                          " duration=%f reset=%d} can't find publish stream",
                          butil::endpoint2str(remote_side()).c_str(), stream_id(),
                          opt.stream_name.c_str(), opt.start, opt.duration,
                          (int)opt.reset);

        LOG(ERROR) << "When play, can't find publish stream_name:" 
                   << opt.stream_name;
        return;
    }

    _stream_name = opt.stream_name;
    _role = RTMP_ROLE_PLAY;

	done->Run();

    // Send metadata.
    SendMetaData(publish_stream->_metadata);

    // Send video header.
    if (publish_stream->_video_header.size() > 1) {
        SendVideoMessage(publish_stream->_video_header);
    }

    // Send audio header.
    if (publish_stream->_audio_header.size() > 1) {
        SendAudioMessage(publish_stream->_audio_header);
    }

    // Send last cache GOP.
    for (const auto& cache : publish_stream->_cacheVideo) {
        SendVideoMessage(cache);
    }

    // Send cache audio message.
    for (const auto& cache : publish_stream->_cacheAudio) {
        SendAudioMessage(cache);
    }

    _service_impl->AddPlayStream(opt.stream_name, this);
}

void SimpleRtmpStream::OnMetaData(brpc::RtmpMetaData* metadata, const butil::StringPiece& name) {
    _metadata = *metadata;
}

void SimpleRtmpStream::OnAudioMessage(brpc::RtmpAudioMessage* msg) {
    if (msg->IsAACSequenceHeader()) {
        _audio_header = *msg;
    }

    if (_service_impl != NULL) {
        for (const auto& client : _service_impl->GetPlayStream(_stream_name)) {
            client->SendAudioMessage(*msg);
        }
    }

    _cacheAudio.push_back(*msg);
}

void SimpleRtmpStream::OnVideoMessage(brpc::RtmpVideoMessage* msg) {
    if (msg->IsAVCSequenceHeader()) {
        _video_header = *msg;
    } else {
        if (msg->frame_type == brpc::FLV_VIDEO_FRAME_KEYFRAME) {
            _cacheVideo.clear();
            _cacheAudio.clear();
        }
        _cacheVideo.push_back(*msg);
    }

    if (_service_impl != NULL) {
        for (const auto& client : _service_impl->GetPlayStream(_stream_name)) {
            client->SendVideoMessage(*msg);
        }
    }
}

brpc::RtmpServerStream* RtmpServiceImpl::NewStream(const brpc::RtmpConnectRequest&) {
    return new SimpleRtmpStream(this);
}

bool RtmpServiceImpl::AddPublishStream(const std::string& stream_name, brpc::RtmpServerStream* stream) {
    if (_publish_stream.count(stream_name) > 0) {
        return false;
    }

    _publish_stream.insert(make_pair(stream_name, stream));

    return true;
}

void RtmpServiceImpl::DelPublishStream(const std::string& stream_name) {
    _publish_stream.erase(stream_name);
}

brpc::RtmpServerStream* RtmpServiceImpl::FindPublishStream(const std::string& stream_name) {
    brpc::RtmpServerStream* stream = NULL;

    auto iter = _publish_stream.find(stream_name);

    if (iter != _publish_stream.end()) {
        stream = iter->second;
    }

    return stream;
}

bool RtmpServiceImpl::AddPlayStream(const std::string& stream_name, brpc::RtmpServerStream* stream) {
    if (_play_stream[stream_name].count(stream) > 0) {
        return false;
    }

    _play_stream[stream_name].insert(stream);

    return true;
}

void RtmpServiceImpl::DelPlayStream(const std::string& stream_name, brpc::RtmpServerStream* stream) {
    _play_stream[stream_name].erase(stream);

    if (_play_stream[stream_name].empty()) {
        _play_stream.erase(stream_name);
    }
}

std::set<brpc::RtmpServerStream*> RtmpServiceImpl::GetPlayStream(const std::string& stream_name) {
    std::set<brpc::RtmpServerStream*> plays;

    auto iter = _play_stream.find(stream_name);

    if (iter != _play_stream.end()) {
        plays = iter->second;
    }

    return plays;
}

}  // namespace example

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Start the server.
    brpc::ServerOptions options;
    options.rtmp_service = new example::RtmpServiceImpl();
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
