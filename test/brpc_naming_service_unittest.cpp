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

#include <stdio.h>
#include <gtest/gtest.h>
#include <vector>
#include "butil/string_printf.h"
#include "butil/files/temp_file.h"
#include "bthread/bthread.h"
#ifdef BAIDU_INTERNAL
#include "brpc/policy/baidu_naming_service.h"
#endif
#include "brpc/policy/consul_naming_service.h"
#include "brpc/policy/domain_naming_service.h"
#include "brpc/policy/file_naming_service.h"
#include "brpc/policy/list_naming_service.h"
#include "brpc/policy/remote_file_naming_service.h"
#include "brpc/policy/discovery_naming_service.h"
#include "echo.pb.h"
#include "brpc/server.h"


namespace brpc {
DECLARE_int32(health_check_interval);

namespace policy {

DECLARE_bool(consul_enable_degrade_to_file_naming_service);
DECLARE_string(consul_file_naming_service_dir);
DECLARE_string(consul_service_discovery_url);
DECLARE_string(discovery_api_addr);
DECLARE_string(discovery_env);
DECLARE_int32(discovery_renew_interval_s);

} // policy
} // brpc

namespace {

bool IsIPListEqual(const std::set<butil::ip_t>& s1, const std::set<butil::ip_t>& s2) {
    if (s1.size() != s2.size()) {
        return false;
    }
    for (auto it1 = s1.begin(), it2 = s2.begin(); it1 != s1.end(); ++it1, ++it2) {
        if (*it1 != *it2) {
            return false;
        }
    }
    return true;
}

TEST(NamingServiceTest, sanity) {
    std::vector<brpc::ServerNode> servers;

#ifdef BAIDU_INTERNAL
    brpc::policy::BaiduNamingService bns;
    ASSERT_EQ(0, bns.GetServers("qa-pbrpc.SAT.tjyx", &servers));
#endif

    brpc::policy::DomainNamingService dns;
    ASSERT_EQ(0, dns.GetServers("baidu.com:1234", &servers));
    ASSERT_EQ(2u, servers.size());
    ASSERT_EQ(1234, servers[0].addr.port);
    ASSERT_EQ(1234, servers[1].addr.port);
    const std::set<butil::ip_t> expected_ips{servers[0].addr.ip, servers[1].addr.ip};

    ASSERT_EQ(0, dns.GetServers("baidu.com", &servers));
    ASSERT_EQ(2u, servers.size());
    const std::set<butil::ip_t> ip_list1{servers[0].addr.ip, servers[1].addr.ip};
    ASSERT_TRUE(IsIPListEqual(expected_ips, ip_list1));
    ASSERT_EQ(80, servers[0].addr.port);
    ASSERT_EQ(80, servers[1].addr.port);

    ASSERT_EQ(0, dns.GetServers("baidu.com:1234/useless1/useless2", &servers));
    ASSERT_EQ(2u, servers.size());
    const std::set<butil::ip_t> ip_list2{servers[0].addr.ip, servers[1].addr.ip};
    ASSERT_TRUE(IsIPListEqual(expected_ips, ip_list2));
    ASSERT_EQ(1234, servers[0].addr.port);
    ASSERT_EQ(1234, servers[1].addr.port);

    ASSERT_EQ(0, dns.GetServers("baidu.com/useless1/useless2", &servers));
    ASSERT_EQ(2u, servers.size());
    const std::set<butil::ip_t> ip_list3{servers[0].addr.ip, servers[1].addr.ip};
    ASSERT_TRUE(IsIPListEqual(expected_ips, ip_list3));
    ASSERT_EQ(80, servers[0].addr.port);
    ASSERT_EQ(80, servers[1].addr.port);

    const char *address_list[] =  {
        "10.127.0.1:1234",
        "10.128.0.1:1234",
        "10.129.0.1:1234",
        "localhost:1234",
        "baidu.com:1234"
    };
    butil::TempFile tmp_file;
    {
        FILE* fp = fopen(tmp_file.fname(), "w");
        for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
            ASSERT_TRUE(fprintf(fp, "%s\n", address_list[i]));
        }
        fclose(fp);
    }
    brpc::policy::FileNamingService fns;
    ASSERT_EQ(0, fns.GetServers(tmp_file.fname(), &servers));
    ASSERT_EQ(ARRAY_SIZE(address_list), servers.size());
    for (size_t i = 0; i < ARRAY_SIZE(address_list) - 2; ++i) {
        std::ostringstream oss;
        oss << servers[i];
        ASSERT_EQ(address_list[i], oss.str()) << "i=" << i;
    }

    std::string s;
    for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
        ASSERT_EQ(0, butil::string_appendf(&s, "%s,", address_list[i]));
    }
    brpc::policy::ListNamingService lns;
    ASSERT_EQ(0, lns.GetServers(s.c_str(), &servers));
    ASSERT_EQ(ARRAY_SIZE(address_list), servers.size());
    for (size_t i = 0; i < ARRAY_SIZE(address_list) - 2; ++i) {
        std::ostringstream oss;
        oss << servers[i];
        ASSERT_EQ(address_list[i], oss.str()) << "i=" << i;
    }
}

TEST(NamingServiceTest, invalid_port) {
    std::vector<brpc::ServerNode> servers;

#ifdef BAIDU_INTERNAL
    brpc::policy::BaiduNamingService bns;
    ASSERT_EQ(0, bns.GetServers("qa-pbrpc.SAT.tjyx:main", &servers));
#endif

    brpc::policy::DomainNamingService dns;
    ASSERT_EQ(-1, dns.GetServers("baidu.com:", &servers));
    ASSERT_EQ(-1, dns.GetServers("baidu.com:123a", &servers));
    ASSERT_EQ(-1, dns.GetServers("baidu.com:99999", &servers));
}

TEST(NamingServiceTest, wrong_name) {
    std::vector<brpc::ServerNode> servers;

#ifdef BAIDU_INTERNAL
    brpc::policy::BaiduNamingService bns;
    ASSERT_EQ(-1, bns.GetServers("Wrong", &servers));
#endif

    const char *address_list[] =  {
        "10.127.0.1:1234",
        "10.128.0.1:12302344",
        "10.129.0.1:1234",
        "10.128.0.1:",
        "10.128.0.1",
        "localhost:1234",
        "baidu.com:1234",
        "LOCAL:1234"
    };
    butil::TempFile tmp_file;
    {
        FILE *fp = fopen(tmp_file.fname(), "w");
        for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
            ASSERT_TRUE(fprintf(fp, "%s\n", address_list[i]));
        }
        fclose(fp);
    }
    brpc::policy::FileNamingService fns;
    ASSERT_EQ(0, fns.GetServers(tmp_file.fname(), &servers));
    ASSERT_EQ(ARRAY_SIZE(address_list) - 4, servers.size());

    std::string s;
    for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
        ASSERT_EQ(0, butil::string_appendf(&s, ", %s", address_list[i]));
    }
    brpc::policy::ListNamingService lns;
    ASSERT_EQ(0, lns.GetServers(s.c_str(), &servers));
    ASSERT_EQ(ARRAY_SIZE(address_list) - 4, servers.size());
}

class UserNamingServiceImpl : public test::UserNamingService {
public:
    UserNamingServiceImpl() : list_names_count(0), touch_count(0) {}
    ~UserNamingServiceImpl() { }
    void ListNames(google::protobuf::RpcController* cntl_base,
                   const test::HttpRequest*,
                   test::HttpResponse*,
                   google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = (brpc::Controller*)cntl_base;
        cntl->http_response().set_content_type("text/plain");
        cntl->response_attachment().append(
            "0.0.0.0:8635 tag1\r\n0.0.0.0:8636 tag2\n"
            "0.0.0.0:8635 tag3\r\n0.0.0.0:8636\r\n");
        list_names_count.fetch_add(1);
    }
    void Touch(google::protobuf::RpcController*,
               const test::HttpRequest*,
               test::HttpResponse*,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        touch_count.fetch_add(1);
    }

    butil::atomic<int64_t> list_names_count;
    butil::atomic<int64_t> touch_count;
};

TEST(NamingServiceTest, remotefile) {
    brpc::Server server1;
    UserNamingServiceImpl svc1;
    ASSERT_EQ(0, server1.AddService(&svc1, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server1.Start("localhost:8635", NULL));
    brpc::Server server2;
    UserNamingServiceImpl svc2;
    ASSERT_EQ(0, server2.AddService(&svc2, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server2.Start("localhost:8636", NULL));

    butil::EndPoint n1;
    ASSERT_EQ(0, butil::str2endpoint("0.0.0.0:8635", &n1));
    butil::EndPoint n2;
    ASSERT_EQ(0, butil::str2endpoint("0.0.0.0:8636", &n2));
    std::vector<brpc::ServerNode> expected_servers;
    expected_servers.push_back(brpc::ServerNode(n1, "tag1"));
    expected_servers.push_back(brpc::ServerNode(n2, "tag2"));
    expected_servers.push_back(brpc::ServerNode(n1, "tag3"));
    expected_servers.push_back(brpc::ServerNode(n2));
    std::sort(expected_servers.begin(), expected_servers.end());

    std::vector<brpc::ServerNode> servers;
    brpc::policy::RemoteFileNamingService rfns;
    ASSERT_EQ(0, rfns.GetServers("0.0.0.0:8635/UserNamingService/ListNames", &servers));
    ASSERT_EQ(expected_servers.size(), servers.size());
    std::sort(servers.begin(), servers.end());
    for (size_t i = 0; i < expected_servers.size(); ++i) {
        ASSERT_EQ(expected_servers[i], servers[i]);
    }

    ASSERT_EQ(0, rfns.GetServers("http://0.0.0.0:8635/UserNamingService/ListNames", &servers));
    ASSERT_EQ(expected_servers.size(), servers.size());
    std::sort(servers.begin(), servers.end());
    for (size_t i = 0; i < expected_servers.size(); ++i) {
        ASSERT_EQ(expected_servers[i], servers[i]);
    }
}

class ConsulNamingServiceImpl : public test::UserNamingService {
public:
  ConsulNamingServiceImpl() : list_names_count(0), touch_count(0) {
  }
    ~ConsulNamingServiceImpl() { }
    void ListNames(google::protobuf::RpcController* cntl_base,
                   const test::HttpRequest*,
                   test::HttpResponse*,
                   google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = (brpc::Controller*)cntl_base;
        cntl->http_response().SetHeader("X-Consul-Index", "1");
        cntl->response_attachment().append(
            R"([
                {
                    "Node": {
                        "ID": "44454c4c-4e00-1050-8052-b7c04f4b5931",
                        "Node": "sh-qs-10.121.36.189",
                        "Address": "10.121.36.189",
                        "Datacenter": "shjj",
                        "TaggedAddresses": {
                            "lan": "10.121.36.189",
                            "wan": "10.121.36.189"
                        },
                        "Meta": {
                            "consul-network-segment": ""
                        },
                        "CreateIndex": 4820296,
                        "ModifyIndex": 4823818
                    },
                    "Service": {
                        "ID": "10.121.36.189_8003_qs_show_leaf",
                        "Service": "qs_show_leaf",
                        "Tags": ["1"],
                        "Address": "10.121.36.189",
                        "Port": 8003,
                        "EnableTagOverride": false,
                        "CreateIndex": 6515285,
                        "ModifyIndex": 6515285
                    },
                    "Checks": [
                        {
                            "Node": "sh-qs-10.121.36.189",
                            "CheckID": "serfHealth",
                            "Name": "Serf Health Status",
                            "Status": "passing",
                            "Notes": "",
                            "Output": "Agent alive and reachable",
                            "ServiceID": "",
                            "ServiceName": "",
                            "ServiceTags": [ ],
                            "CreateIndex": 4820296,
                            "ModifyIndex": 4820296
                        },
                        {
                            "Node": "sh-qs-10.121.36.189",
                            "CheckID": "service:10.121.36.189_8003_qs_show_leaf",
                            "Name": "Service 'qs_show_leaf' check",
                            "Status": "passing",
                            "Notes": "",
                            "Output": "TCP connect 10.121.36.189:8003: Success",
                            "ServiceID": "10.121.36.189_8003_qs_show_leaf",
                            "ServiceName": "qs_show_leaf",
                            "ServiceTags": [ ],
                            "CreateIndex": 6515285,
                            "ModifyIndex": 6702198
                        }
                    ]
                },
                {
                    "Node": {
                        "ID": "44454c4c-4b00-1050-8052-b6c04f4b5931",
                        "Node": "sh-qs-10.121.36.190",
                        "Address": "10.121.36.190",
                        "Datacenter": "shjj",
                        "TaggedAddresses": {
                            "lan": "10.121.36.190",
                            "wan": "10.121.36.190"
                        },
                        "Meta": {
                            "consul-network-segment": ""
                        },
                        "CreateIndex": 4820296,
                        "ModifyIndex": 4823751
                    },
                    "Service": {
                        "ID": "10.121.36.190_8003_qs_show_leaf",
                        "Service": "qs_show_leaf",
                        "Tags": ["2"],
                        "Address": "10.121.36.190",
                        "Port": 8003,
                        "EnableTagOverride": false,
                        "CreateIndex": 6515635,
                        "ModifyIndex": 6515635
                    },
                    "Checks": [
                        {
                            "Node": "sh-qs-10.121.36.190",
                            "CheckID": "serfHealth",
                            "Name": "Serf Health Status",
                            "Status": "passing",
                            "Notes": "",
                            "Output": "Agent alive and reachable",
                            "ServiceID": "",
                            "ServiceName": "",
                            "ServiceTags": [ ],
                            "CreateIndex": 4820296,
                            "ModifyIndex": 4820296
                        },
                        {
                            "Node": "sh-qs-10.121.36.190",
                            "CheckID": "service:10.121.36.190_8003_qs_show_leaf",
                            "Name": "Service 'qs_show_leaf' check",
                            "Status": "passing",
                            "Notes": "",
                            "Output": "TCP connect 10.121.36.190:8003: Success",
                            "ServiceID": "10.121.36.190_8003_qs_show_leaf",
                            "ServiceName": "qs_show_leaf",
                            "ServiceTags": [ ],
                            "CreateIndex": 6515635,
                            "ModifyIndex": 6705515
                        }
                    ]
                }
            ])");
        list_names_count.fetch_add(1);
    }
    void Touch(google::protobuf::RpcController*,
               const test::HttpRequest*,
               test::HttpResponse*,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        touch_count.fetch_add(1);
    }

    butil::atomic<int64_t> list_names_count;
    butil::atomic<int64_t> touch_count;
};

TEST(NamingServiceTest, consul_with_backup_file) {
    brpc::policy::FLAGS_consul_enable_degrade_to_file_naming_service = true;
    const int saved_hc_interval = brpc::FLAGS_health_check_interval;
    brpc::FLAGS_health_check_interval = 1;
    const char *address_list[] =  {
        "10.127.0.1:1234",
        "10.128.0.1:1234",
        "10.129.0.1:1234",
    };
    butil::TempFile tmp_file;
    const char * service_name = tmp_file.fname();
    {
        FILE* fp = fopen(tmp_file.fname(), "w");
        for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
            ASSERT_TRUE(fprintf(fp, "%s\n", address_list[i]));
        }
        fclose(fp);
    }
    std::cout << tmp_file.fname() << std::endl;

    std::vector<brpc::ServerNode> servers;
    brpc::policy::ConsulNamingService cns;
    ASSERT_EQ(0, cns.GetServers(service_name, &servers));
    ASSERT_EQ(ARRAY_SIZE(address_list), servers.size());
    for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
        std::ostringstream oss;
        oss << servers[i];
        ASSERT_EQ(address_list[i], oss.str()) << "i=" << i;
    }

    brpc::Server server;
    ConsulNamingServiceImpl svc;
    std::string restful_map(brpc::policy::FLAGS_consul_service_discovery_url);
    restful_map.append("/");
    restful_map.append(service_name);
    restful_map.append("   => ListNames");
    ASSERT_EQ(0, server.AddService(&svc,
                                   brpc::SERVER_DOESNT_OWN_SERVICE,
                                   restful_map.c_str()));
    ASSERT_EQ(0, server.Start("localhost:8500", NULL));

    bthread_usleep(5000000);

    butil::EndPoint n1;
    ASSERT_EQ(0, butil::str2endpoint("10.121.36.189:8003", &n1));
    butil::EndPoint n2;
    ASSERT_EQ(0, butil::str2endpoint("10.121.36.190:8003", &n2));
    std::vector<brpc::ServerNode> expected_servers;
    expected_servers.push_back(brpc::ServerNode(n1, "1"));
    expected_servers.push_back(brpc::ServerNode(n2, "2"));
    std::sort(expected_servers.begin(), expected_servers.end());

    servers.clear();
    ASSERT_EQ(0, cns.GetServers(service_name, &servers));
    ASSERT_EQ(expected_servers.size(), servers.size());
    std::sort(servers.begin(), servers.end());
    for (size_t i = 0; i < expected_servers.size(); ++i) {
        ASSERT_EQ(expected_servers[i], servers[i]);
    }
    brpc::FLAGS_health_check_interval = saved_hc_interval;
}


static const std::string s_fetchs_result = R"({
    "code":0,
    "message":"0",
    "ttl":1,
    "data":{
        "admin.test":{
            "instances":[
                {
                    "region":"",
                    "zone":"sh001",
                    "env":"uat",
                    "appid":"admin.test",
                    "treeid":0,
                    "hostname":"host123",
                    "http":"",
                    "rpc":"",
                    "version":"123",
                    "metadata":{
                        "weight": "10",
                        "cluster": ""
                    },
                    "addrs":[
                        "http://127.0.0.1:8999",
                        "grpc://127.0.1.1:9000"
                    ],
                    "status":1,
                    "reg_timestamp":1539001034551496412,
                    "up_timestamp":1539001034551496412,
                    "renew_timestamp":1539001034551496412,
                    "dirty_timestamp":1539001034551496412,
                    "latest_timestamp":1539001034551496412
                }
            ],
            "zone_instances":{
                "sh001":[
                    {
                        "region":"",
                        "zone":"sh001",
                        "env":"uat",
                        "appid":"admin.test",
                        "treeid":0,
                        "hostname":"host123",
                        "http":"",
                        "rpc":"",
                        "version":"123",
                        "metadata":{
                            "weight": "10",
                            "cluster": ""
                        },
                        "addrs":[
                            "http://127.0.0.1:8999",
                            "grpc://127.0.1.1:9000"
                        ],
                        "status":1,
                        "reg_timestamp":1539001034551496412,
                        "up_timestamp":1539001034551496412,
                        "renew_timestamp":1539001034551496412,
                        "dirty_timestamp":1539001034551496412,
                        "latest_timestamp":1539001034551496412
                    }
                ]
            },
            "latest_timestamp":1539001034551496412,
            "latest_timestamp_str":"1539001034"
        }
    }
})";

static std::string s_nodes_result = R"({
    "code": 0,
    "message": "0",
    "ttl": 1,
    "data": [
        {
            "addr": "127.0.0.1:8635",
            "status": 0,
            "zone": ""
        }, {
            "addr": "172.18.33.51:7171",
            "status": 0,
            "zone": ""
        }, {
            "addr": "172.18.33.52:7171",
            "status": 0,
            "zone": ""
        }
    ]
})";


class DiscoveryNamingServiceImpl : public test::DiscoveryNamingService {
public:
    DiscoveryNamingServiceImpl()
        : _renew_count(0)
        , _cancel_count(0) {}
    virtual ~DiscoveryNamingServiceImpl() {}

    void Nodes(google::protobuf::RpcController* cntl_base,
               const test::HttpRequest*,
               test::HttpResponse*,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append(s_nodes_result);
    }

    void Fetchs(google::protobuf::RpcController* cntl_base,
                const test::HttpRequest*,
                test::HttpResponse*,
                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append(s_fetchs_result);
    }

    void Register(google::protobuf::RpcController* cntl_base,
                 const test::HttpRequest*,
                 test::HttpResponse*,
                 google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append(R"({
            "code": 0,
            "message": "0"
        })");
        return;
    }

    void Renew(google::protobuf::RpcController* cntl_base,
               const test::HttpRequest*,
               test::HttpResponse*,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append(R"({
            "code": 0,
            "message": "0"
        })");
        _renew_count++;
        return;
    }

    void Cancel(google::protobuf::RpcController* cntl_base,
                const test::HttpRequest*,
                test::HttpResponse*,
                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append(R"({
            "code": 0,
            "message": "0"
        })");
        _cancel_count++;
        return;
    }

    int RenewCount() const { return _renew_count; }
    int CancelCount() const { return _cancel_count; }

private:
    int _renew_count;
    int _cancel_count;
};

TEST(NamingServiceTest, discovery_sanity) {
    brpc::policy::FLAGS_discovery_api_addr = "http://127.0.0.1:8635/discovery/nodes";
    brpc::policy::FLAGS_discovery_renew_interval_s = 1;
    brpc::Server server;
    DiscoveryNamingServiceImpl svc;
    std::string rest_mapping =
        "/discovery/nodes => Nodes, "
        "/discovery/fetchs => Fetchs, "
        "/discovery/register => Register, "
        "/discovery/renew => Renew, "
        "/discovery/cancel => Cancel";
    ASSERT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE,
                rest_mapping.c_str()));
    ASSERT_EQ(0, server.Start("localhost:8635", NULL));

    brpc::policy::DiscoveryNamingService dcns;
    std::vector<brpc::ServerNode> servers;
    ASSERT_EQ(0, dcns.GetServers("admin.test", &servers));
    ASSERT_EQ((size_t)1, servers.size());

    brpc::policy::DiscoveryRegisterParam dparam;
    dparam.appid = "main.test";
    dparam.hostname = "hostname";
    dparam.addrs = "grpc://10.0.0.1:8000";
    dparam.env = "dev";
    dparam.zone = "sh001";
    dparam.status = 1;
    dparam.version = "v1";
    {
        brpc::policy::DiscoveryClient dc;
    }
    // Cancel is called iff Register is called
    ASSERT_EQ(svc.CancelCount(), 0);
    {
        brpc::policy::DiscoveryClient dc;
        // Two Register should start one Renew task , and make
        // svc.RenewCount() be one.
        ASSERT_EQ(0, dc.Register(dparam));
        ASSERT_EQ(0, dc.Register(dparam));
        bthread_usleep(100000);
    }
    ASSERT_EQ(svc.RenewCount(), 1);
    ASSERT_EQ(svc.CancelCount(), 1);
}

} //namespace
