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


#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <butil/macros.h>
#include <butil/file_util.h>
#include <bvar/bvar.h>
#include <bthread/bthread.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include <brpc/rpc_dump.h>
#include <brpc/serialized_request.h>
#include <brpc/nshead_message.h>
#include <brpc/details/http_message.h>
#include "brpc/options.pb.h"
#include "info_thread.h"

DEFINE_string(dir, "", "The directory of dumped requests");
DEFINE_int32(times, 1, "Repeat replaying for so many times");
DEFINE_int32(qps, 0, "Limit QPS if this flag is positive");
DEFINE_int32(thread_num, 0, "Number of threads for replaying");
DEFINE_bool(use_bthread, true, "Use bthread to replay");
DEFINE_string(connection_type, "", "Connection type, choose automatically "
              "according to protocol by default");
DEFINE_string(server, "0.0.0.0:8002", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Maximum retry times");
DEFINE_int32(dummy_port, 8899, "Port of dummy server(to monitor replaying)");
DEFINE_string(http_host, "", "Host field for http protocol");

bvar::LatencyRecorder g_latency_recorder("rpc_replay");
bvar::Adder<int64_t> g_error_count("rpc_replay_error_count");
bvar::Adder<int64_t> g_sent_count;

// Include channels for all protocols that support both client and server.
class ChannelGroup {
public:
    int Init();

    ~ChannelGroup();

    // Get channel by protocol type.
    brpc::Channel* channel(brpc::ProtocolType type) {
        if ((size_t)type < _chans.size()) {
            return _chans[(size_t)type];
        }
        return NULL;
    }
    
private:
    std::vector<brpc::Channel*> _chans;
};

int ChannelGroup::Init() {
    {
        // force global initialization of rpc.
        brpc::Channel dummy_channel;
    }
    std::vector<std::pair<brpc::ProtocolType, brpc::Protocol> > protocols;
    brpc::ListProtocols(&protocols);
    size_t max_protocol_size = 0;
    for (size_t i = 0; i < protocols.size(); ++i) {
        max_protocol_size = std::max(max_protocol_size,
                                     (size_t)protocols[i].first);
    }
    _chans.resize(max_protocol_size + 1);
    for (size_t i = 0; i < protocols.size(); ++i) {
        const brpc::ProtocolType protocol_type = protocols[i].first;
        const brpc::Protocol protocol = protocols[i].second;
        brpc::ChannelOptions options;
        options.protocol = protocol_type;
        options.connection_type = FLAGS_connection_type;
        options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
        options.max_retry = FLAGS_max_retry;
        if ((options.connection_type == brpc::CONNECTION_TYPE_UNKNOWN || 
            options.connection_type & protocol.supported_connection_type) &&
            protocol.support_client() &&
            protocol.support_server()) {
            brpc::Channel* chan = new brpc::Channel;
            if (chan->Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(),
                        &options) != 0) {
                LOG(ERROR) << "Fail to initialize channel";
                delete chan;
                return -1;
            }
            _chans[protocol_type] = chan;
        }
    }
    return 0;
}

ChannelGroup::~ChannelGroup() {
    for (size_t i = 0; i < _chans.size(); ++i) {
        delete _chans[i];
    }
    _chans.clear();
}

static void handle_response(brpc::Controller* cntl, int64_t start_time,
                            bool sleep_on_error/*note*/) {
    // TODO(gejun): some bthreads are starved when new bthreads are created 
    // continuously, which happens when server is down and RPC keeps failing.
    // Sleep a while on error to avoid that now.
    const int64_t end_time = butil::gettimeofday_us();
    const int64_t elp = end_time - start_time;
    if (!cntl->Failed()) {
        g_latency_recorder << elp;
    } else {
        g_error_count << 1;
        if (sleep_on_error) {
            bthread_usleep(10000);
        }
    }
    delete cntl;
}

butil::atomic<int> g_thread_offset(0);

static void* replay_thread(void* arg) {
    ChannelGroup* chan_group = static_cast<ChannelGroup*>(arg);
    const int thread_offset = g_thread_offset.fetch_add(1, butil::memory_order_relaxed);
    double req_rate = FLAGS_qps / (double)FLAGS_thread_num;
    brpc::SerializedRequest req;
    brpc::NsheadMessage nshead_req;
    int64_t last_expected_time = butil::monotonic_time_ns();
    const int64_t interval = (int64_t) (1000000000L / req_rate);
    // the max tolerant delay between end_time and expected_time. 10ms or 10 intervals
    int64_t max_tolerant_delay = std::max((int64_t) 10000000L, 10 * interval);
    for (int i = 0; !brpc::IsAskedToQuit() && i < FLAGS_times; ++i) {
        brpc::SampleIterator it(FLAGS_dir);
        int j = 0;
        for (brpc::SampledRequest* sample = it.Next();
             !brpc::IsAskedToQuit() && sample != NULL; sample = it.Next(), ++j) {
            std::unique_ptr<brpc::SampledRequest> sample_guard(sample);
            if ((j % FLAGS_thread_num) != thread_offset) {
                continue;
            }
            brpc::Channel* chan =
                chan_group->channel(sample->meta.protocol_type());
            if (chan == NULL) {
                LOG(ERROR) << "No channel on protocol="
                           << sample->meta.protocol_type();
                continue;
            }
            
            brpc::Controller* cntl = new brpc::Controller;
            req.Clear();
            
            google::protobuf::Message* req_ptr = &req;
            cntl->reset_sampled_request(sample_guard.release());
            if (sample->meta.protocol_type() == brpc::PROTOCOL_HTTP) {
                brpc::HttpMessage http_message;
                http_message.ParseFromIOBuf(sample->request);
                cntl->http_request().Swap(http_message.header());
                if (!FLAGS_http_host.empty()) {
                    // reset Host in header
                    cntl->http_request().SetHeader("Host", FLAGS_http_host);
                }
                cntl->request_attachment() = http_message.body().movable();
                req_ptr = NULL;
            } else if (sample->meta.protocol_type() == brpc::PROTOCOL_NSHEAD) {
                nshead_req.Clear();
                memcpy(&nshead_req.head, sample->meta.nshead().c_str(), sample->meta.nshead().length());
                nshead_req.body = sample->request;
                req_ptr = &nshead_req;
            } else if (sample->meta.attachment_size() > 0) {
                sample->request.cutn(
                    &req.serialized_data(),
                    sample->request.size() - sample->meta.attachment_size());
                cntl->request_attachment() = sample->request.movable();
            } else {
                req.serialized_data() = sample->request.movable();
            }
            g_sent_count << 1;
            const int64_t start_time = butil::gettimeofday_us();
            if (FLAGS_qps <= 0) {
                chan->CallMethod(NULL/*use rpc_dump_context in cntl instead*/,
                        cntl, req_ptr, NULL/*ignore response*/, NULL);
                handle_response(cntl, start_time, true);
            } else {
                google::protobuf::Closure* done =
                    brpc::NewCallback(handle_response, cntl, start_time, false);
                chan->CallMethod(NULL/*use rpc_dump_context in cntl instead*/,
                        cntl, req_ptr, NULL/*ignore response*/, done);
                int64_t end_time = butil::monotonic_time_ns();
                int64_t expected_time = last_expected_time + interval;
                if (end_time < expected_time) {
                    usleep((expected_time - end_time)/1000);
                }
                if (end_time - expected_time > max_tolerant_delay) {
                    expected_time = end_time;
                }            
                last_expected_time = expected_time;
            }
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_dir.empty() ||
        !butil::DirectoryExists(butil::FilePath(FLAGS_dir))) {
        LOG(ERROR) << "--dir=<dir-of-dumped-files> is required";
        return -1;
    }

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }
    
    ChannelGroup chan_group;
    if (chan_group.Init() != 0) {
        LOG(ERROR) << "Fail to init ChannelGroup";
        return -1;
    }

    if (FLAGS_thread_num <= 0) {
        if (FLAGS_qps <= 0) { // unlimited qps
            FLAGS_thread_num = 50;
        } else {
            FLAGS_thread_num = FLAGS_qps / 10000;
            if (FLAGS_thread_num < 1) {
                FLAGS_thread_num = 1;
            }
            if (FLAGS_thread_num > 50) {
                FLAGS_thread_num = 50;
            }
        }
    }

    const int rate_limit_per_thread = 1000000;
    int req_rate_per_thread = FLAGS_qps / FLAGS_thread_num;
    if (req_rate_per_thread > rate_limit_per_thread) {
        LOG(ERROR) << "req_rate: " << (int64_t) req_rate_per_thread << " is too large in one thread. The rate limit is " 
                <<  rate_limit_per_thread << " in one thread";
        return -1;
    }    

    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    if (!FLAGS_use_bthread) {
        pids.resize(FLAGS_thread_num);
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (pthread_create(&pids[i], NULL, replay_thread, &chan_group) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        }
    } else {
        bids.resize(FLAGS_thread_num);
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (bthread_start_background(
                    &bids[i], NULL, replay_thread, &chan_group) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }
    brpc::InfoThread info_thr;
    brpc::InfoThreadOptions info_thr_opt;
    info_thr_opt.latency_recorder = &g_latency_recorder;
    info_thr_opt.error_count = &g_error_count;
    info_thr_opt.sent_count = &g_sent_count;
    
    if (!info_thr.start(info_thr_opt)) {
        LOG(ERROR) << "Fail to create info_thread";
        return -1;
    }

    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(pids[i], NULL);
        } else {
            bthread_join(bids[i], NULL);
        }
    }
    info_thr.stop();

    return 0;
}
