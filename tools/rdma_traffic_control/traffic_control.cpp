// Copyright (c) 2014 baidu-rpc authors.
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

// Authors: Li,Zhaogeng (lizhaogeng01@baidu.com)

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/rdma_traffic_control.pb.h>

DEFINE_string(server, "0.0.0.0:8002", "The server to control the traffic");
DEFINE_bool(disable, false, "Disable the RDMA traffic or not");
DEFINE_string(socket_id_to_disable, "", "Socket id lists that would like to disable, with colon separated");

void ParseDisabledConns(std::vector<brpc::SocketId>& disabled_conns, const std::string flags) {
    std::string tmp;
    std::istringstream iss;
    brpc::SocketId socket_id;

    std::string::size_type pos1 = 0;
    std::string::size_type pos2 = flags.find(":");
    while (pos2 != std::string::npos) {
        iss.str("");
        iss.clear();
        tmp = flags.substr(pos1, pos2 - pos1);
        iss.str(tmp);
        iss >> socket_id;
        disabled_conns.push_back(socket_id);

        pos1 = pos2 + 1;
        pos2 = flags.find(":", pos1);
    }
    iss.str("");
    iss.clear();
    tmp = flags.substr(pos1);
    iss.str(tmp);
    iss >> socket_id;
    disabled_conns.push_back(socket_id);
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel;
    if (channel.Init(FLAGS_server.c_str(), NULL) != 0) {
        LOG(ERROR) << "Fail to init channel";
        return -1;
    }

    brpc::rdma::RdmaTrafficControlService_Stub stub(&channel);
    brpc::Controller cntl;
    brpc::rdma::RdmaTrafficControlResponse response;
    brpc::rdma::RdmaTrafficControlRequest request;
    std::vector<brpc::SocketId> disabled_conns;

    if (FLAGS_disable) {
        stub.TurnOff(&cntl, &request, &response, NULL);
    } else if (std::strcmp(FLAGS_socket_id_to_disable.c_str(), "") != 0) {
            ParseDisabledConns(disabled_conns, FLAGS_socket_id_to_disable);

            for (std::vector<brpc::SocketId>::iterator it = disabled_conns.begin();
                it != disabled_conns.end(); it++) {
                request.add_socket_id(*it);
            }

            stub.TurnOffPartially(&cntl, &request, &response, NULL);
    } else {
        stub.TurnOn(&cntl, &request, &response, NULL);
    }
    if (cntl.Failed()) {
        LOG(ERROR) << "Failed";
    } else {
        LOG(INFO) << "Success";
    }
    return 0;
}
