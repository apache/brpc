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

// A server to receive EchoRequest and send back EchoResponse asynchronously.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include "echo.pb.h"

DEFINE_bool(echo_attachment, true, "Echo attachment as well");
DEFINE_int32(port, 8002, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

butil::atomic<int> nsd(0);
struct MySessionLocalData {
    MySessionLocalData() : x(123) {
        nsd.fetch_add(1, butil::memory_order_relaxed);
    }
    ~MySessionLocalData() {
        nsd.fetch_sub(1, butil::memory_order_relaxed);
    }

    int x;
};

class MySessionLocalDataFactory : public brpc::DataFactory {
public:
    void* CreateData() const {
        return new MySessionLocalData;
    }

    void DestroyData(void* d) const {
        delete static_cast<MySessionLocalData*>(d);
    }
};

butil::atomic<int> ntls(0);
struct MyThreadLocalData {
    MyThreadLocalData() : y(0) {
        ntls.fetch_add(1, butil::memory_order_relaxed);
    }
    ~MyThreadLocalData() {
        ntls.fetch_sub(1, butil::memory_order_relaxed);
    }
    static void deleter(void* d) {
        delete static_cast<MyThreadLocalData*>(d);
    }

    int y;
};

class MyThreadLocalDataFactory : public brpc::DataFactory {
public:
    void* CreateData() const {
        return new MyThreadLocalData;
    }

    void DestroyData(void* d) const {
        MyThreadLocalData::deleter(d);
    }
};

struct AsyncJob {
    MySessionLocalData* expected_session_local_data;
    int expected_session_value;
    brpc::Controller* cntl;
    const example::EchoRequest* request;
    example::EchoResponse* response;
    google::protobuf::Closure* done;

    void run();
    
    void run_and_delete() {
        run();
        delete this;
    }
};

static void* process_thread(void* args) {
    AsyncJob* job = static_cast<AsyncJob*>(args);
    job->run_and_delete();
    return NULL;
}

// Your implementation of example::EchoService
class EchoServiceWithThreadAndSessionLocal : public example::EchoService {
public:
    EchoServiceWithThreadAndSessionLocal() {
        CHECK_EQ(0, bthread_key_create(&_tls2_key, MyThreadLocalData::deleter));
    }
    ~EchoServiceWithThreadAndSessionLocal() {
        CHECK_EQ(0, bthread_key_delete(_tls2_key));
    };
    void Echo(google::protobuf::RpcController* cntl_base,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        // Get the session-local data which is created by ServerOptions.session_local_data_factory
        // and reused between different RPC. All session-local data are
        // destroyed upon server destruction.
        MySessionLocalData* sd = static_cast<MySessionLocalData*>(cntl->session_local_data());
        if (sd == NULL) {
            cntl->SetFailed("Require ServerOptions.session_local_data_factory to be"
                            " set with a correctly implemented instance");
            LOG(ERROR) << cntl->ErrorText();
            return;
        }
        const int expected_value = sd->x + (((uintptr_t)cntl) & 0xFFFFFFFF);
        sd->x = expected_value;

        // Get the thread-local data which is created by ServerOptions.thread_local_data_factory
        // and reused between different threads. All thread-local data are 
        // destroyed upon server destruction.
        // "tls" is short for "thread local storage".
        MyThreadLocalData* tls =
            static_cast<MyThreadLocalData*>(brpc::thread_local_data());
        if (tls == NULL) {
            cntl->SetFailed("Require ServerOptions.thread_local_data_factory "
                            "to be set with a correctly implemented instance");
            LOG(ERROR) << cntl->ErrorText();
            return;
        }
        tls->y = expected_value;

        // You can create bthread-local data for your own.
        // The interfaces are similar with pthread equivalence:
        //   pthread_key_create  -> bthread_key_create
        //   pthread_key_delete  -> bthread_key_delete
        //   pthread_getspecific -> bthread_getspecific
        //   pthread_setspecific -> bthread_setspecific
        MyThreadLocalData* tls2 = 
            static_cast<MyThreadLocalData*>(bthread_getspecific(_tls2_key));
        if (tls2 == NULL) {
            tls2 = new MyThreadLocalData;
            CHECK_EQ(0, bthread_setspecific(_tls2_key, tls2));
        }
        tls2->y = expected_value + 1;
        
        // sleep awhile to force context switching.
        bthread_usleep(10000);

        // tls is unchanged after context switching.
        CHECK_EQ(tls, brpc::thread_local_data());
        CHECK_EQ(expected_value, tls->y);

        CHECK_EQ(tls2, bthread_getspecific(_tls2_key));
        CHECK_EQ(expected_value + 1, tls2->y);

        // Process the request asynchronously.
        AsyncJob* job = new AsyncJob;
        job->expected_session_local_data = sd;
        job->expected_session_value = expected_value;
        job->cntl = cntl;
        job->request = request;
        job->response = response;
        job->done = done;
        bthread_t th;
        CHECK_EQ(0, bthread_start_background(&th, NULL, process_thread, job));

        // We don't want to call done->Run() here, release the guard.
        done_guard.release();
        
        LOG_EVERY_SECOND(INFO) << "ntls=" << ntls.load(butil::memory_order_relaxed)
                               << " nsd=" << nsd.load(butil::memory_order_relaxed);
    }

private:
    bthread_key_t _tls2_key;
};

void AsyncJob::run() {
    brpc::ClosureGuard done_guard(done);

    // Sleep some time to make sure that Echo() exits.
    bthread_usleep(10000);    

    // Still the session-local data that we saw in Echo().
    // This is the major difference between session-local data and thread-local
    // data which was already destroyed upon Echo() exit.
    MySessionLocalData* sd = static_cast<MySessionLocalData*>(cntl->session_local_data());
    CHECK_EQ(expected_session_local_data, sd);
    CHECK_EQ(expected_session_value, sd->x);

    // Echo request and its attachment
    response->set_message(request->message());
    if (FLAGS_echo_attachment) {
        cntl->response_attachment().append(cntl->request_attachment());
    }
}    

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // The factory to create MySessionLocalData. Must be valid when server is running.
    MySessionLocalDataFactory session_local_data_factory;

    MyThreadLocalDataFactory thread_local_data_factory;

    // Generally you only need one Server.
    brpc::Server server;
    // For more options see `brpc/server.h'.
    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;
    options.session_local_data_factory = &session_local_data_factory;
    options.thread_local_data_factory = &thread_local_data_factory;

    // Instance of your service.
    EchoServiceWithThreadAndSessionLocal echo_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server. 
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    CHECK_EQ(ntls, 0);
    CHECK_EQ(nsd, 0);
    return 0;
}
