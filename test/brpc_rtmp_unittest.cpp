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

// brpc - A framework to host and access services throughout Baidu.

// Date: Fri May 20 15:52:22 CST 2016

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/controller.h"
#include "brpc/rtmp.h"
#include "brpc/amf.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

class TestRtmpClientStream : public brpc::RtmpClientStream {
public:
    TestRtmpClientStream()
        : _called_on_stop(0)
        , _called_on_first_message(0)
        , _nvideomsg(0)
        , _naudiomsg(0) {
        LOG(INFO) << __FUNCTION__;
    }
    ~TestRtmpClientStream() {
        LOG(INFO) << __FUNCTION__;
        assertions_on_stop();
    }
    void assertions_on_stop() {
        ASSERT_EQ(1, _called_on_stop);
    }
    void assertions_on_successful_play() {
        ASSERT_EQ(1, _called_on_first_message);
        ASSERT_LT(0, _nvideomsg);
        ASSERT_LT(0, _naudiomsg);
    }
    void assertions_on_failure() {
        ASSERT_EQ(0, _called_on_first_message);
        ASSERT_EQ(0, _nvideomsg);
        ASSERT_EQ(0, _naudiomsg);
        assertions_on_stop();
    }
    void OnFirstMessage() {
        ++_called_on_first_message;
    }
    void OnStop() {
        ++_called_on_stop;
    }
    void OnVideoMessage(brpc::RtmpVideoMessage* msg) {
        ++_nvideomsg;
        // video data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
    void OnAudioMessage(brpc::RtmpAudioMessage* msg) {
        ++_naudiomsg;
        // audio data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
private:
    int _called_on_stop;
    int _called_on_first_message;
    int _nvideomsg;
    int _naudiomsg;
};

class TestRtmpRetryingClientStream
    : public brpc::RtmpRetryingClientStream {
public:
    TestRtmpRetryingClientStream()
        : _called_on_stop(0)
        , _called_on_first_message(0)
        , _called_on_playable(0) {
        LOG(INFO) << __FUNCTION__;
    }
    ~TestRtmpRetryingClientStream() {
        LOG(INFO) << __FUNCTION__;
        assertions_on_stop();
    }
    void assertions_on_stop() {
        ASSERT_EQ(1, _called_on_stop);
    }
    void OnStop() {
        ++_called_on_stop;
    }
    void OnFirstMessage() {
        ++_called_on_first_message;
    }
    void OnPlayable() {
        ++_called_on_playable;
    }

    void OnVideoMessage(brpc::RtmpVideoMessage* msg) {
        // video data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
    void OnAudioMessage(brpc::RtmpAudioMessage* msg) {
        // audio data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
private:
    int _called_on_stop;
    int _called_on_first_message;
    int _called_on_playable;
};

const char* UNEXIST_NAME = "unexist_stream";

class PlayingDummyStream : public brpc::RtmpServerStream {
public:
    enum State {
        STATE_UNPLAYING,
        STATE_PLAYING,
        STATE_STOPPED
    };
    PlayingDummyStream(int64_t sleep_ms)
        : _state(STATE_UNPLAYING), _sleep_ms(sleep_ms) {
        LOG(INFO) << __FUNCTION__ << "(" << this << ")";
    }
    ~PlayingDummyStream() {
        LOG(INFO) << __FUNCTION__ << "(" << this << ")";
    }
    void OnPlay(const brpc::RtmpPlayOptions& opt,
                butil::Status* status,
                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got play{stream_name=" << opt.stream_name
                  << " start=" << opt.start
                  << " duration=" << opt.duration
                  << " reset=" << opt.reset << '}';
        if (opt.stream_name == UNEXIST_NAME) {
            status->set_error(EPERM, "Unexist stream");
            return;
        }
        if (_sleep_ms > 0) {
            LOG(INFO) << "Sleep " << _sleep_ms
                      << " ms before responding play request";
            bthread_usleep(_sleep_ms * 1000L);
        }
        int rc = bthread_start_background(&_play_thread, NULL,
                                          RunSendData, this);
        if (rc) {
            status->set_error(rc, "Fail to create thread");
            return;
        }
        State expected = STATE_UNPLAYING;
        if (!_state.compare_exchange_strong(expected, STATE_PLAYING)) {
            if (expected == STATE_STOPPED) {
                bthread_stop(_play_thread);
                bthread_join(_play_thread, NULL);
            } else {
                CHECK(false) << "Impossible";
            }
        }
    }

    void OnStop() {
        LOG(INFO) << "OnStop of PlayingDummyStream=" << this;
        if (_state.exchange(STATE_STOPPED) == STATE_PLAYING) {
            bthread_stop(_play_thread);
            bthread_join(_play_thread, NULL);
        }
    }

    void SendData();
    
private:
    static void* RunSendData(void* arg) {
        ((PlayingDummyStream*)arg)->SendData();
        return NULL;
    }

    butil::atomic<State> _state;
    bthread_t _play_thread;
    int64_t _sleep_ms;
};

void PlayingDummyStream::SendData() {
    LOG(INFO) << "Enter SendData of PlayingDummyStream=" << this;

    brpc::RtmpVideoMessage vmsg;
    brpc::RtmpAudioMessage amsg;

    vmsg.timestamp = 1000;
    amsg.timestamp = 1000;
    for (int i = 0; !bthread_stopped(bthread_self()); ++i) {
        vmsg.timestamp += 20;
        amsg.timestamp += 20;

        vmsg.frame_type = brpc::FLV_VIDEO_FRAME_KEYFRAME;
        vmsg.codec = brpc::FLV_VIDEO_AVC;
        vmsg.data.clear();
        vmsg.data.append(butil::string_printf("video_%d(ms_id=%u)",
                                             i, stream_id()));
        //failing to send is possible
        SendVideoMessage(vmsg);

        amsg.codec = brpc::FLV_AUDIO_AAC;
        amsg.rate = brpc::FLV_SOUND_RATE_44100HZ;
        amsg.bits = brpc::FLV_SOUND_16BIT;
        amsg.type = brpc::FLV_SOUND_STEREO;
        amsg.data.clear();
        amsg.data.append(butil::string_printf("audio_%d(ms_id=%u)",
                                             i, stream_id()));
        SendAudioMessage(amsg);

        bthread_usleep(1000000);
    }

    LOG(INFO) << "Quit SendData of PlayingDummyStream=" << this;
}

class PlayingDummyService : public brpc::RtmpService {
public:
    PlayingDummyService(int64_t sleep_ms = 0) : _sleep_ms(sleep_ms) {}

private:
    // Called to create a server-side stream.
    virtual brpc::RtmpServerStream* NewStream(
        const brpc::RtmpConnectRequest&) {
        return new PlayingDummyStream(_sleep_ms);
    }
    int64_t _sleep_ms;
};

class PublishStream : public brpc::RtmpServerStream {
public:
    PublishStream(int64_t sleep_ms)
        : _sleep_ms(sleep_ms)
        , _called_on_stop(0)
        , _called_on_first_message(0)
        , _nvideomsg(0)
        , _naudiomsg(0) {
        LOG(INFO) << __FUNCTION__ << "(" << this << ")";
    }
    ~PublishStream() {
        LOG(INFO) << __FUNCTION__ << "(" << this << ")";
        assertions_on_stop();
    }
    void assertions_on_stop() {
        ASSERT_EQ(1, _called_on_stop);
    }
    void OnPublish(const std::string& stream_name,
                   brpc::RtmpPublishType publish_type,
                   butil::Status* status,
                   google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got publish{stream_name=" << stream_name
                  << " type=" << brpc::RtmpPublishType2Str(publish_type)
                  << '}';
        if (stream_name == UNEXIST_NAME) {
            status->set_error(EPERM, "Unexist stream");
            return;
        }
        if (_sleep_ms > 0) {
            LOG(INFO) << "Sleep " << _sleep_ms
                      << " ms before responding play request";
            bthread_usleep(_sleep_ms * 1000L);
        }
    }
    void OnFirstMessage() {
        ++_called_on_first_message;
    }
    void OnStop() {
        LOG(INFO) << "OnStop of PublishStream=" << this;
        ++_called_on_stop;
    }
    void OnVideoMessage(brpc::RtmpVideoMessage* msg) {
        ++_nvideomsg;
        // video data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
    void OnAudioMessage(brpc::RtmpAudioMessage* msg) {
        ++_naudiomsg;
        // audio data is ascii in UT, print it out.
        LOG(INFO) << remote_side() << "|stream=" << stream_id()
                  << ": Got " << *msg << " data=" << msg->data;
    }
private:
    int64_t _sleep_ms;
    int _called_on_stop;
    int _called_on_first_message;
    int _nvideomsg;
    int _naudiomsg;
};

class PublishService : public brpc::RtmpService {
public:
    PublishService(int64_t sleep_ms = 0) : _sleep_ms(sleep_ms) {
        pthread_mutex_init(&_mutex, NULL);
    }
    ~PublishService() {
        pthread_mutex_destroy(&_mutex);
    }
    void move_created_streams(
        std::vector<butil::intrusive_ptr<PublishStream> >* out) {
        out->clear();
        BAIDU_SCOPED_LOCK(_mutex);
        out->swap(_created_streams);
    }

private:
    // Called to create a server-side stream.
    virtual brpc::RtmpServerStream* NewStream(
        const brpc::RtmpConnectRequest&) {
        PublishStream* stream = new PublishStream(_sleep_ms);
        {
            BAIDU_SCOPED_LOCK(_mutex);
            _created_streams.push_back(stream);
        }
        return stream;
    }
    int64_t _sleep_ms;
    pthread_mutex_t _mutex;
    std::vector<butil::intrusive_ptr<PublishStream> > _created_streams;
};

class RtmpSubStream : public brpc::RtmpClientStream {
public:
    explicit RtmpSubStream(brpc::RtmpMessageHandler* mh)
        : _message_handler(mh) {}
    // @RtmpStreamBase
    void OnMetaData(brpc::RtmpMetaData*, const butil::StringPiece&);
    void OnSharedObjectMessage(brpc::RtmpSharedObjectMessage* msg);
    void OnAudioMessage(brpc::RtmpAudioMessage* msg);
    void OnVideoMessage(brpc::RtmpVideoMessage* msg);
    void OnFirstMessage();
    void OnStop();
private:
    std::unique_ptr<brpc::RtmpMessageHandler> _message_handler;
};

void RtmpSubStream::OnFirstMessage() {
    _message_handler->OnPlayable();
}

void RtmpSubStream::OnMetaData(brpc::RtmpMetaData* obj, const butil::StringPiece& name) {
    _message_handler->OnMetaData(obj, name);
}

void RtmpSubStream::OnSharedObjectMessage(brpc::RtmpSharedObjectMessage* msg) {
    _message_handler->OnSharedObjectMessage(msg);
}

void RtmpSubStream::OnAudioMessage(brpc::RtmpAudioMessage* msg) {
    _message_handler->OnAudioMessage(msg);
}

void RtmpSubStream::OnVideoMessage(brpc::RtmpVideoMessage* msg) {
    _message_handler->OnVideoMessage(msg);
}

void RtmpSubStream::OnStop() {
    _message_handler->OnSubStreamStop(this);
}


class RtmpSubStreamCreator : public brpc::SubStreamCreator {
public:
    RtmpSubStreamCreator(const brpc::RtmpClient* client);

    ~RtmpSubStreamCreator();

    // @SubStreamCreator
    void NewSubStream(brpc::RtmpMessageHandler* message_handler,
                      butil::intrusive_ptr<brpc::RtmpStreamBase>* sub_stream);
    void LaunchSubStream(brpc::RtmpStreamBase* sub_stream,
                         brpc::RtmpRetryingClientStreamOptions* options);

private:
    const brpc::RtmpClient* _client;
};

RtmpSubStreamCreator::RtmpSubStreamCreator(const brpc::RtmpClient* client)
    : _client(client) {}

RtmpSubStreamCreator::~RtmpSubStreamCreator() {}
 
void RtmpSubStreamCreator::NewSubStream(brpc::RtmpMessageHandler* message_handler,
                                        butil::intrusive_ptr<brpc::RtmpStreamBase>* sub_stream) {
    if (sub_stream) { 
        (*sub_stream).reset(new RtmpSubStream(message_handler));
    }
    return;
}

void RtmpSubStreamCreator::LaunchSubStream(
    brpc::RtmpStreamBase* sub_stream, 
    brpc::RtmpRetryingClientStreamOptions* options) {
    brpc::RtmpClientStreamOptions client_options = *options;
    dynamic_cast<RtmpSubStream*>(sub_stream)->Init(_client, client_options);
}

TEST(RtmpTest, parse_rtmp_url) {
    butil::StringPiece host;
    butil::StringPiece vhost;
    butil::StringPiece port;
    butil::StringPiece app;
    butil::StringPiece stream_name;

    brpc::ParseRtmpURL("rtmp://HOST/APP/STREAM",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_TRUE(vhost.empty());
    ASSERT_EQ("1935", port);
    ASSERT_EQ("APP", app);
    ASSERT_EQ("STREAM", stream_name);

    brpc::ParseRtmpURL("HOST/APP/STREAM",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_TRUE(vhost.empty());
    ASSERT_EQ("1935", port);
    ASSERT_EQ("APP", app);
    ASSERT_EQ("STREAM", stream_name);

    brpc::ParseRtmpURL("rtmp://HOST:8765//APP?vhost=abc///STREAM?queries",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_EQ("abc", vhost);
    ASSERT_EQ("8765", port);
    ASSERT_EQ("APP", app);
    ASSERT_EQ("STREAM?queries", stream_name);

    brpc::ParseRtmpURL("HOST:8765//APP?vhost=abc///STREAM?queries",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_EQ("abc", vhost);
    ASSERT_EQ("8765", port);
    ASSERT_EQ("APP", app);
    ASSERT_EQ("STREAM?queries", stream_name);

    brpc::ParseRtmpURL("HOST:8765//APP?vhost=abc///STREAM?queries/",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_EQ("abc", vhost);
    ASSERT_EQ("8765", port);
    ASSERT_EQ("APP", app);
    ASSERT_EQ("STREAM?queries/", stream_name);

    brpc::ParseRtmpURL("HOST:8765/APP?vhost=abc",
                             &host, &vhost, &port, &app, &stream_name);
    ASSERT_EQ("HOST", host);
    ASSERT_EQ("abc", vhost);
    ASSERT_EQ("8765", port);
    ASSERT_EQ("APP", app);
    ASSERT_TRUE(stream_name.empty());
}

TEST(RtmpTest, amf) {
    std::string req_buf;
    brpc::RtmpInfo info;
    brpc::AMFObject obj;
    std::string dummy = "_result";
    {
        google::protobuf::io::StringOutputStream zc_stream(&req_buf);
        brpc::AMFOutputStream ostream(&zc_stream);
        brpc::WriteAMFString(dummy, &ostream);
        brpc::WriteAMFUint32(17, &ostream);
        info.set_code("NetConnection.Connect"); // TODO
        info.set_level("error");
        info.set_description("heheda hello foobar");
        brpc::WriteAMFObject(info, &ostream);
        ASSERT_TRUE(ostream.good());
        obj.SetString("code", "foo");
        obj.SetString("level", "bar");
        obj.SetString("description", "heheda");
        brpc::WriteAMFObject(obj, &ostream);
        ASSERT_TRUE(ostream.good());
    }

    google::protobuf::io::ArrayInputStream zc_stream(req_buf.data(), req_buf.size());
    brpc::AMFInputStream istream(&zc_stream);
    std::string result;
    ASSERT_TRUE(brpc::ReadAMFString(&result, &istream));
    ASSERT_EQ(dummy, result);
    uint32_t num = 0;
    ASSERT_TRUE(brpc::ReadAMFUint32(&num, &istream));
    ASSERT_EQ(17u, num);
    brpc::RtmpInfo info2;
    ASSERT_TRUE(brpc::ReadAMFObject(&info2, &istream));
    ASSERT_EQ(info.code(), info2.code());
    ASSERT_EQ(info.level(), info2.level());
    ASSERT_EQ(info.description(), info2.description());
    brpc::RtmpInfo info3;
    ASSERT_TRUE(brpc::ReadAMFObject(&info3, &istream));
    ASSERT_EQ("foo", info3.code());
    ASSERT_EQ("bar", info3.level());
    ASSERT_EQ("heheda", info3.description());
}

TEST(RtmpTest, successfully_play_streams) {
    PlayingDummyService rtmp_service;
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8571, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8571", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        //opt.publish_name = butil::string_printf("pub_name_%d", i);
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
    }
    sleep(5);
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i]->assertions_on_successful_play();
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, fail_to_play_streams) {
    PlayingDummyService rtmp_service;
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8571, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8571", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.play_name = UNEXIST_NAME;
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
    }
    sleep(1);
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i]->assertions_on_failure();
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, successfully_publish_streams) {
    PublishService rtmp_service;
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8571, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8571", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.publish_name = butil::string_printf("pub_name_%d", i);
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
    }
    const int REP = 5;
    for (int i = 0; i < REP; ++i) {
        brpc::RtmpVideoMessage vmsg;
        vmsg.timestamp = 1000 + i * 20;
        vmsg.frame_type = brpc::FLV_VIDEO_FRAME_KEYFRAME;
        vmsg.codec = brpc::FLV_VIDEO_AVC;
        vmsg.data.append(butil::string_printf("video_%d", i));
        for (int j = 0; j < NSTREAM; j += 2) {
            ASSERT_EQ(0, cstreams[j]->SendVideoMessage(vmsg));
        }
        
        brpc::RtmpAudioMessage amsg;
        amsg.timestamp = 1000 + i * 20;
        amsg.codec = brpc::FLV_AUDIO_AAC;
        amsg.rate = brpc::FLV_SOUND_RATE_44100HZ;
        amsg.bits = brpc::FLV_SOUND_16BIT;
        amsg.type = brpc::FLV_SOUND_STEREO;
        amsg.data.append(butil::string_printf("audio_%d", i));
        for (int j = 1; j < NSTREAM; j += 2) {
            ASSERT_EQ(0, cstreams[j]->SendAudioMessage(amsg));
        }
        
        bthread_usleep(500000);
    }
    std::vector<butil::intrusive_ptr<PublishStream> > created_streams;
    rtmp_service.move_created_streams(&created_streams);
    ASSERT_EQ(NSTREAM, (int)created_streams.size());
    for (int i = 0; i < NSTREAM; ++i) {
        EXPECT_EQ(1, created_streams[i]->_called_on_first_message);
    }
    for (int j = 0; j < NSTREAM; j += 2) {
        ASSERT_EQ(REP, created_streams[j]->_nvideomsg);
    }
    for (int j = 1; j < NSTREAM; j += 2) {
        ASSERT_EQ(REP, created_streams[j]->_naudiomsg);
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, failed_to_publish_streams) {
    PublishService rtmp_service;
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8575, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8575", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.publish_name = UNEXIST_NAME;
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
    }
    sleep(1);
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i]->assertions_on_failure();
    }
    std::vector<butil::intrusive_ptr<PublishStream> > created_streams;
    rtmp_service.move_created_streams(&created_streams);
    ASSERT_EQ(NSTREAM, (int)created_streams.size());
    for (int i = 0; i < NSTREAM; ++i) {
        ASSERT_EQ(0, created_streams[i]->_called_on_first_message);
        ASSERT_EQ(0, created_streams[i]->_nvideomsg);
        ASSERT_EQ(0, created_streams[i]->_naudiomsg);
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, failed_to_connect_client_streams) {
    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8572", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
        cstreams[i]->assertions_on_failure();
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, destroy_client_streams_before_init) {
    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8573", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    butil::intrusive_ptr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        cstreams[i]->Destroy();
        ASSERT_EQ(1, cstreams[i]->_called_on_stop);
        ASSERT_EQ(brpc::RtmpClientStream::STATE_DESTROYING, cstreams[i]->_state);
        brpc::RtmpClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        opt.wait_until_play_or_publish_is_sent = true;
        cstreams[i]->Init(&rtmp_client, opt);
        cstreams[i]->assertions_on_failure();
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, destroy_retrying_client_streams_before_init) {
    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8573", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    butil::intrusive_ptr<TestRtmpRetryingClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpRetryingClientStream);
        cstreams[i]->Destroy();
        ASSERT_EQ(1, cstreams[i]->_called_on_stop);
        brpc::RtmpRetryingClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        brpc::SubStreamCreator* sc = new RtmpSubStreamCreator(&rtmp_client);
        cstreams[i]->Init(sc, opt);
        ASSERT_EQ(1, cstreams[i]->_called_on_stop);
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, destroy_client_streams_during_creation) {
    PlayingDummyService rtmp_service(2000/*sleep 2s*/);
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8574, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8574", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    butil::intrusive_ptr<TestRtmpClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpClientStream);
        brpc::RtmpClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        cstreams[i]->Init(&rtmp_client, opt);
        ASSERT_EQ(0, cstreams[i]->_called_on_stop);
        usleep(500*1000);
        ASSERT_EQ(0, cstreams[i]->_called_on_stop);
        cstreams[i]->Destroy();
        usleep(10*1000);
        ASSERT_EQ(1, cstreams[i]->_called_on_stop);
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, destroy_retrying_client_streams_during_creation) {
    PlayingDummyService rtmp_service(2000/*sleep 2s*/);
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8574, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8574", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    butil::intrusive_ptr<TestRtmpRetryingClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpRetryingClientStream);
        brpc::RtmpRetryingClientStreamOptions opt;
        opt.play_name = butil::string_printf("play_name_%d", i);
        brpc::SubStreamCreator* sc = new RtmpSubStreamCreator(&rtmp_client);
        cstreams[i]->Init(sc, opt);
        ASSERT_EQ(0, cstreams[i]->_called_on_stop);
        usleep(500*1000);
        ASSERT_EQ(0, cstreams[i]->_called_on_stop);
        cstreams[i]->Destroy();
        usleep(10*1000);
        ASSERT_EQ(1, cstreams[i]->_called_on_stop);
    }
    LOG(INFO) << "Quiting program...";
}

TEST(RtmpTest, retrying_stream) {
    PlayingDummyService rtmp_service;
    brpc::Server server;
    brpc::ServerOptions server_opt;
    server_opt.rtmp_service = &rtmp_service;
    ASSERT_EQ(0, server.Start(8576, &server_opt));

    brpc::RtmpClientOptions rtmp_opt;
    rtmp_opt.app = "hello";
    rtmp_opt.swfUrl = "anything";
    rtmp_opt.tcUrl = "rtmp://heheda";
    brpc::RtmpClient rtmp_client;
    ASSERT_EQ(0, rtmp_client.Init("localhost:8576", rtmp_opt));

    // Create multiple streams.
    const int NSTREAM = 2;
    brpc::DestroyingPtr<TestRtmpRetryingClientStream> cstreams[NSTREAM];
    for (int i = 0; i < NSTREAM; ++i) {
        cstreams[i].reset(new TestRtmpRetryingClientStream);
        brpc::Controller cntl;
        brpc::RtmpRetryingClientStreamOptions opt;
        opt.play_name = butil::string_printf("name_%d", i);
        brpc::SubStreamCreator* sc = new RtmpSubStreamCreator(&rtmp_client);
        cstreams[i]->Init(sc, opt);
    }
    sleep(3);
    LOG(INFO) << "Stopping server";
    server.Stop(0);
    server.Join();
    LOG(INFO) << "Stopped server and sleep for a while";
    sleep(3);
    ASSERT_EQ(0, server.Start(8576, &server_opt));
    sleep(3);
    for (int i = 0; i < NSTREAM; ++i) {
        ASSERT_EQ(1, cstreams[i]->_called_on_first_message);
        ASSERT_EQ(2, cstreams[i]->_called_on_playable);
    }
    LOG(INFO) << "Quiting program...";
}
