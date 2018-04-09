// Copyright (c) 2014 Baidu, Inc.
// Date 2014/10/20 13:50:10

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
#include "echo.pb.h"
#include "brpc/server.h"


namespace brpc {
namespace policy {

DECLARE_bool(consul_enable_degrade_to_file_naming_service);
DECLARE_string(consul_file_naming_service_dir);
DECLARE_string(consul_service_discovery_url);

} // policy
} // brpc

namespace {
TEST(NamingServiceTest, sanity) {
    std::vector<brpc::ServerNode> servers;

#ifdef BAIDU_INTERNAL
    brpc::policy::BaiduNamingService bns;
    ASSERT_EQ(0, bns.GetServers("qa-pbrpc.SAT.tjyx", &servers));
#endif

    brpc::policy::DomainNamingService dns;
    ASSERT_EQ(0, dns.GetServers("brpc.baidu.com:1234", &servers));
    ASSERT_EQ(1u, servers.size());
    ASSERT_EQ(1234, servers[0].addr.port);
    const butil::ip_t expected_ip = servers[0].addr.ip;

    ASSERT_EQ(0, dns.GetServers("brpc.baidu.com", &servers));
    ASSERT_EQ(1u, servers.size());
    ASSERT_EQ(expected_ip, servers[0].addr.ip);
    ASSERT_EQ(80, servers[0].addr.port);

    ASSERT_EQ(0, dns.GetServers("brpc.baidu.com:1234/useless1/useless2", &servers));
    ASSERT_EQ(1u, servers.size());
    ASSERT_EQ(expected_ip, servers[0].addr.ip);
    ASSERT_EQ(1234, servers[0].addr.port);

    ASSERT_EQ(0, dns.GetServers("brpc.baidu.com/useless1/useless2", &servers));
    ASSERT_EQ(1u, servers.size());
    ASSERT_EQ(expected_ip, servers[0].addr.ip);
    ASSERT_EQ(80, servers[0].addr.port);

    const char *address_list[] =  {
        "10.127.0.1:1234",
        "10.128.0.1:1234",
        "10.129.0.1:1234",
        "localhost:1234",
        "brpc.baidu.com:1234"
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
    ASSERT_EQ(-1, dns.GetServers("brpc.baidu.com:", &servers));
    ASSERT_EQ(-1, dns.GetServers("brpc.baidu.com:123a", &servers));
    ASSERT_EQ(-1, dns.GetServers("brpc.baidu.com:99999", &servers));
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
        "brpc.baidu.com:1234",
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

    bthread_usleep(1000000);

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
}

} //namespace
