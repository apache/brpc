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
#include <brpc/server.h>
#include <brpc/channel.h>
#include "view.pb.h"

DEFINE_int32(port, 8888, "TCP Port of this server");
DEFINE_string(target, "", "The server to view");

// handle HTTP response of accessing builtin services of the target server.
static void handle_response(brpc::Controller* client_cntl,
                            std::string target,
                            brpc::Controller* server_cntl,
                            google::protobuf::Closure* server_done) {
    // Copy all headers. The "Content-Length" will be overwriteen.
    server_cntl->http_response() = client_cntl->http_response();
    // Copy content.
    server_cntl->response_attachment() = client_cntl->response_attachment();
    // Insert "rpc_view: <target>" before </body> so that users are always
    // visually notified with target server w/o confusions.
    butil::IOBuf& content = server_cntl->response_attachment();
    butil::IOBuf before_body;
    if (content.cut_until(&before_body, "</body>") == 0) {
        before_body.append(
            "<style type=\"text/css\">\n"
            ".rpcviewlogo {position: fixed; bottom: 0px; right: 0px;"
            " color: #ffffff; background-color: #000000; }\n"
            " </style>\n"
            "<span class='rpcviewlogo'>&nbsp;rpc_view: ");
        before_body.append(target);
        before_body.append("&nbsp;</span></body>");
        before_body.append(content);
        content = before_body;
    }
    // Notice that we don't set RPC to failed on http errors because we
    // want to pass unchanged content to the users otherwise RPC replaces
    // the content with ErrorText.
    if (client_cntl->Failed() &&
        client_cntl->ErrorCode() != brpc::EHTTP) {
        server_cntl->SetFailed(client_cntl->ErrorCode(),
                               "%s", client_cntl->ErrorText().c_str());
    }
    delete client_cntl;
    server_done->Run();
}

// A http_master_service.
class ViewServiceImpl : public ViewService {
public:
    ViewServiceImpl() {}
    virtual ~ViewServiceImpl() {};
    virtual void default_method(google::protobuf::RpcController* cntl_base,
                                const HttpRequest*,
                                HttpResponse*,
                                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* server_cntl =
            static_cast<brpc::Controller*>(cntl_base);

        // Get or set target. Notice that we don't access FLAGS_target directly
        // which is thread-unsafe (for string flags).
        std::string target;
        const std::string* newtarget =
            server_cntl->http_request().uri().GetQuery("changetarget");
        if (newtarget) {
            if (GFLAGS_NS::SetCommandLineOption("target", newtarget->c_str()).empty()) {
                server_cntl->SetFailed("Fail to change value of -target");
                return;
            }
            target = *newtarget;
        } else {
            if (!GFLAGS_NS::GetCommandLineOption("target", &target)) {
                server_cntl->SetFailed("Fail to get value of -target");
                return;
            }
        }

        // Create the http channel on-the-fly. Notice that we've set 
        // `defer_close_second' in main() so that dtor of channels do not
        // close connections immediately and ad-hoc creation of channels 
        // often reuses the not-yet-closed connections.
        brpc::Channel http_chan;
        brpc::ChannelOptions http_chan_opt;
        http_chan_opt.protocol = brpc::PROTOCOL_HTTP;
        if (http_chan.Init(target.c_str(), &http_chan_opt) != 0) {
            server_cntl->SetFailed(brpc::EINTERNAL,
                                   "Fail to connect to %s", target.c_str());
            return;
        }

        // Remove "Accept-Encoding". We need to insert additional texts
        // before </body>, preventing the server from compressing the content
        // simplifies our code. The additional bandwidth consumption shouldn't
        // be an issue for infrequent checking out of builtin services pages.
        server_cntl->http_request().RemoveHeader("Accept-Encoding");

        brpc::Controller* client_cntl = new brpc::Controller;
        client_cntl->http_request() = server_cntl->http_request();
        // Remove "Host" so that RPC will laterly serialize the (correct)
        // target server in.
        client_cntl->http_request().RemoveHeader("host");

        // Setup the URI.
        const brpc::URI& server_uri = server_cntl->http_request().uri();
        std::string uri = server_uri.path();
        if (!server_uri.query().empty()) {
            uri.push_back('?');
            uri.append(server_uri.query());
        }
        if (!server_uri.fragment().empty()) {
            uri.push_back('#');
            uri.append(server_uri.fragment());
        }
        client_cntl->http_request().uri() = uri;

        // /hotspots pages may take a long time to finish, since they all have
        // query "seconds", we set the timeout to be longer than "seconds".
        const std::string* seconds =
            server_cntl->http_request().uri().GetQuery("seconds");
        int64_t timeout_ms = 5000;
        if (seconds) {
            timeout_ms += atoll(seconds->c_str()) * 1000;
        }
        client_cntl->set_timeout_ms(timeout_ms);

        // Keep content as it is.
        client_cntl->request_attachment() = server_cntl->request_attachment();
        
        http_chan.CallMethod(NULL, client_cntl, NULL, NULL,
                             brpc::NewCallback(
                                 handle_response, client_cntl, target,
                                 server_cntl, done_guard.release()));
    }
};

int main(int argc, char* argv[]) {
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_target.empty() &&
        (argc != 2 || 
         GFLAGS_NS::SetCommandLineOption("target", argv[1]).empty())) {
        LOG(ERROR) << "Usage: ./rpc_view <ip>:<port>";
        return -1;
    }
    // This keeps ad-hoc creation of channels reuse previous connections.
    GFLAGS_NS::SetCommandLineOption("defer_close_seconds", "10");

    brpc::Server server;
    server.set_version("rpc_view_server");
    brpc::ServerOptions server_opt;
    server_opt.http_master_service = new ViewServiceImpl;
    if (server.Start(FLAGS_port, &server_opt) != 0) {
        LOG(ERROR) << "Fail to start ViewServer";
        return -1;
    }
    server.RunUntilAskedToQuit();
    return 0;
}
