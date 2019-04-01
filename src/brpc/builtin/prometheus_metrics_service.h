// Copyright (c) 2018 BiliBili, Inc.
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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#ifndef BRPC_PROMETHEUS_METRICS_SERVICE_H
#define BRPC_PROMETHEUS_METRICS_SERVICE_H

#include "brpc/builtin_service.pb.h"
#include "brpc/server.h"

namespace brpc {

class PrometheusMetricsService : public metrics {
public:
    PrometheusMetricsService(Server* server)
        : _server(server) {}

    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::MetricsRequest* request,
                        ::brpc::MetricsResponse* response,
                        ::google::protobuf::Closure* done) override;
private:
    Server* _server;
};

} // namepace brpc

#endif  // BRPC_PROMETHEUS_METRICS_SERVICE_H
