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

#include "echo.brpc.fb.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

#define CODEGEN_CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error("check failed: " #condition); \
        } \
    } while (0)

using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;
using brpc::flatbuffers::MethodDescriptor;
using brpc::flatbuffers::ServiceDescriptor;
using codegen::example::Echo;
using codegen::example::Other;
using codegen::example::Request;
using codegen::example::Response;
using google::protobuf::Closure;
using google::protobuf::RpcController;

class Controller : public RpcController {
public:
    void Reset() override { failures = 0; error.clear(); }
    bool Failed() const override { return failures != 0; }
    std::string ErrorText() const override { return error; }
    void StartCancel() override {}
    void SetFailed(const std::string& reason) override { ++failures; error = reason; }
    bool IsCanceled() const override { return false; }
    void NotifyOnCancel(Closure*) override {}
    int failures = 0;
    std::string error;
};

class Done : public Closure {
public:
    void Run() override { ++runs; }
    int runs = 0;
};

class DeletingDone : public Closure {
public:
    explicit DeletingDone(int* runs) : runs_(runs) {}
    void Run() override { ++*runs_; delete this; }
private:
    int* runs_;
};

Message MakeRequest(const char* text) {
    MessageBuilder builder;
    const auto value = text ? builder.CreateString(text) :
                             ::flatbuffers::Offset<::flatbuffers::String>();
    builder.Finish(codegen::example::CreateRequest(builder, value));
    return builder.ReleaseMessage();
}

class Implementation : public Echo {
public:
    void Repeat(RpcController*, const Message* request,
                Message* response, Closure* done) override {
        ++calls;
        selected = 41;
        Reply(request, response, done);
    }
    void Inspect(RpcController*, const Message* request,
                 Message* response, Closure* done) override {
        ++calls;
        selected = 7;
        Reply(request, response, done);
    }
    void Reply(const Message* request, Message* response, Closure* done) {
        const auto* input = request->GetRoot<Request>();
        MessageBuilder builder;
        const auto value = input->text() ? builder.CreateString(input->text()->str()) :
                                          ::flatbuffers::Offset<::flatbuffers::String>();
        builder.Finish(codegen::example::CreateResponse(builder, value));
        *response = builder.ReleaseMessage();
        if (done) {
            done->Run();
        }
    }
    int calls = 0;
    int selected = -1;
};

class LocalChannel : public brpc::flatbuffers::RpcChannel {
public:
    explicit LocalChannel(Echo* service, int* destroyed = nullptr)
        : service_(service), destroyed_(destroyed) {}
    ~LocalChannel() override { if (destroyed_) { ++*destroyed_; } }
    void FBCallMethod(const MethodDescriptor* method, RpcController* controller,
                      const Message* request, Message* response, Closure* done) override {
        last_method = method;
        service_->FBCallMethod(method, controller, request, response, done);
    }
    const MethodDescriptor* last_method = nullptr;
private:
    Echo* service_;
    int* destroyed_;
};

void TestConcurrentDescriptors() {
    const int thread_count = 24;
    std::atomic<bool> start(false);
    std::atomic<int> invalid(0);
    std::vector<const ServiceDescriptor*> descriptors(thread_count);
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i] {
            while (!start.load()) {
                std::this_thread::yield();
            }
            for (int n = 0; n < 1000; ++n) {
                descriptors[i] = Echo::descriptor();
                if (descriptors[i]->method_count() != 2 ||
                    descriptors[i]->method(0)->index() != 41 ||
                    descriptors[i]->method(1)->index() != 7) {
                    ++invalid;
                }
            }
        });
    }
    start.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
    CODEGEN_CHECK(invalid.load() == 0);
    for (const auto* descriptor : descriptors) {
        CODEGEN_CHECK(descriptor == descriptors.front());
    }
    const auto* descriptor = descriptors.front();
    CODEGEN_CHECK(descriptor->full_name() == "codegen.example.Echo");
    CODEGEN_CHECK(descriptor->FindMethodByIndex(41) == descriptor->method(0));
    CODEGEN_CHECK(descriptor->FindMethodByIndex(7) == descriptor->method(1));
    CODEGEN_CHECK(descriptor->FindMethodByIndex(0) == nullptr);
    CODEGEN_CHECK(descriptor->method(0)->service() == descriptor);
}

void ExpectFailure(const std::function<void(Controller*, Done*)>& call) {
    Controller controller;
    Done done;
    call(&controller, &done);
    CODEGEN_CHECK(controller.Failed());
    CODEGEN_CHECK(controller.failures == 1);
    CODEGEN_CHECK(!controller.ErrorText().empty());
    CODEGEN_CHECK(done.runs == 1);
}

void TestDispatchAndErrors() {
    Implementation service;
    LocalChannel channel(&service);
    Echo::Stub stub(&channel);
    const auto* descriptor = Echo::descriptor();
    CODEGEN_CHECK(stub.GetDescriptor() == descriptor);
    CODEGEN_CHECK(stub.channel() == &channel);
    for (const char* text : {static_cast<const char*>(nullptr), "hello"}) {
        Message request = MakeRequest(text);
        for (int position = 0; position < 2; ++position) {
            Controller controller;
            Done done;
            Message response;
            if (position == 0) {
                stub.Repeat(&controller, &request, &response, &done);
            } else {
                stub.Inspect(&controller, &request, &response, &done);
            }
            CODEGEN_CHECK(!controller.Failed());
            CODEGEN_CHECK(done.runs == 1);
            CODEGEN_CHECK(channel.last_method == descriptor->method(position));
            CODEGEN_CHECK(service.selected == (position == 0 ? 41 : 7));
            CODEGEN_CHECK(response.Verify<Response>());
            const auto* output = response.GetRoot<Response>();
            CODEGEN_CHECK(text ? output->text() && output->text()->str() == text :
                         output->text() == nullptr);
        }
    }
    const int calls = service.calls;
    Message request = MakeRequest(nullptr);
    Message response;
    Message invalid;
    const MethodDescriptor unknown("Unknown", descriptor, 99);
    const MethodDescriptor forged("Forged", descriptor, 41);
    for (const auto* method : {static_cast<const MethodDescriptor*>(nullptr),
            Other::descriptor()->method(0), &unknown, &forged}) {
        ExpectFailure([&](Controller* controller, Done* done) {
            service.FBCallMethod(method, controller, &request, &response, done);
        });
    }
    for (int position = 0; position < 2; ++position) {
        ExpectFailure([&](Controller* controller, Done* done) {
            service.FBCallMethod(descriptor->method(position), controller,
                                 &invalid, &response, done);
        });
    }
    ExpectFailure([&](Controller* controller, Done* done) {
        service.FBCallMethod(descriptor->method(0), controller, nullptr, &response, done);
    });
    ExpectFailure([&](Controller* controller, Done* done) {
        service.FBCallMethod(descriptor->method(0), controller, &request, nullptr, done);
    });
    CODEGEN_CHECK(service.calls == calls);

    Echo unimplemented;
    for (int position = 0; position < 2; ++position) {
        ExpectFailure([&](Controller* controller, Done* done) {
            unimplemented.FBCallMethod(descriptor->method(position), controller,
                                       &request, &response, done);
        });
    }
    ExpectFailure([&](Controller* controller, Done* done) {
        unimplemented.Repeat(controller, &request, &response, done);
    });
    Echo::Stub disconnected(nullptr);
    ExpectFailure([&](Controller* controller, Done* done) {
        disconnected.Repeat(controller, &request, &response, done);
    });
    ExpectFailure([&](Controller* controller, Done* done) {
        disconnected.Inspect(controller, &request, &response, done);
    });
    Controller controller;
    service.FBCallMethod(nullptr, &controller, &request, &response, nullptr);
    CODEGEN_CHECK(controller.failures == 1);
    Done done;
    service.FBCallMethod(nullptr, nullptr, &request, &response, &done);
    CODEGEN_CHECK(done.runs == 1);
    int deleted_runs = 0;
    service.FBCallMethod(nullptr, nullptr, &request, &response,
                         new DeletingDone(&deleted_runs));
    CODEGEN_CHECK(deleted_runs == 1);
}

void TestOwnershipAndAsyncCompletion() {
    Implementation service;
    int destroyed = 0;
    {
        Echo::Stub owner(new LocalChannel(&service, &destroyed),
                         brpc::flatbuffers::Service::STUB_OWNS_CHANNEL);
    }
    CODEGEN_CHECK(destroyed == 1);
    {
        LocalChannel borrowed(&service, &destroyed);
        {
            Echo::Stub stub(&borrowed);
        }
        CODEGEN_CHECK(destroyed == 1);
    }
    CODEGEN_CHECK(destroyed == 2);

    class Deferred : public Echo {
    public:
        void Repeat(RpcController*, const Message*, Message*, Closure* done) override {
            pending = done;
        }
        Closure* pending = nullptr;
    } deferred;
    Message request = MakeRequest(nullptr);
    Message response;
    Controller controller;
    Done done;
    deferred.FBCallMethod(Echo::descriptor()->method(0), &controller,
                          &request, &response, &done);
    CODEGEN_CHECK(!controller.Failed());
    CODEGEN_CHECK(done.runs == 0);
    CODEGEN_CHECK(deferred.pending == &done);
    deferred.pending->Run();
    CODEGEN_CHECK(done.runs == 1);
}

}  // namespace

int main() {
    try {
        TestConcurrentDescriptors();
        TestDispatchAndErrors();
        TestOwnershipAndAsyncCompletion();
        std::cout << "Descriptor, sparse dispatch, verifier, callback and ownership checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
