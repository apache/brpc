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

#include <gtest/gtest.h>
#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <butil/fd_guard.h>
#include <butil/fd_utility.h>
#include "butil/errno.h"
#include "butil/endpoint.h"
#include "butil/logging.h"
#include "butil/memory/scope_guard.h"
#include "butil/containers/flat_map.h"
#include "butil/details/extended_endpoint.hpp"
#include <netinet/tcp.h>
#if defined(OS_LINUX)
#include <sys/epoll.h>
#elif defined(OS_MACOSX)
#include <sys/event.h>
#include <netinet/tcp_fsm.h>
#endif

namespace butil {
int pthread_fd_wait(int fd, unsigned events, const timespec* abstime);
int pthread_timed_connect(int sockfd, const struct sockaddr* serv_addr,
                          socklen_t addrlen, const timespec* abstime);
}

namespace {

using butil::details::ExtendedEndPoint;

TEST(EndPointTest, comparisons) {
    butil::EndPoint p1(butil::int2ip(1234), 5678);
    butil::EndPoint p2 = p1;
    ASSERT_TRUE(p1 == p2 && !(p1 != p2));
    ASSERT_TRUE(p1 <= p2 && p1 >= p2 && !(p1 < p2 || p1 > p2));
    ++p2.port;
    ASSERT_TRUE(p1 != p2 && !(p1 == p2));
    ASSERT_TRUE(p1 < p2 && p2 > p1 && !(p2 <= p1 || p1 >= p2));
    --p2.port;
    p2.ip = butil::int2ip(butil::ip2int(p2.ip)-1);
    ASSERT_TRUE(p1 != p2 && !(p1 == p2));
    ASSERT_TRUE(p1 > p2 && p2 < p1 && !(p1 <= p2 || p2 >= p1));
}

TEST(EndPointTest, ip_t) {
    LOG(INFO) << "INET_ADDRSTRLEN = " << INET_ADDRSTRLEN;
    
    butil::ip_t ip0;
    ASSERT_EQ(0, butil::str2ip("1.1.1.1", &ip0));
    ASSERT_STREQ("1.1.1.1", butil::ip2str(ip0).c_str());
    ASSERT_EQ(-1, butil::str2ip("301.1.1.1", &ip0));
    ASSERT_EQ(-1, butil::str2ip("1.-1.1.1", &ip0));
    ASSERT_EQ(-1, butil::str2ip("1.1.-101.1", &ip0));
    ASSERT_STREQ("1.0.0.0", butil::ip2str(butil::int2ip(1)).c_str());

    butil::ip_t ip1, ip2, ip3;
    ASSERT_EQ(0, butil::str2ip("192.168.0.1", &ip1));
    ASSERT_EQ(0, butil::str2ip("192.168.0.2", &ip2));
    ip3 = ip1;
    ASSERT_LT(ip1, ip2);
    ASSERT_LE(ip1, ip2);
    ASSERT_GT(ip2, ip1);
    ASSERT_GE(ip2, ip1);
    ASSERT_TRUE(ip1 != ip2);
    ASSERT_FALSE(ip1 == ip2);
    ASSERT_TRUE(ip1 == ip3);
    ASSERT_FALSE(ip1 != ip3);
}

TEST(EndPointTest, show_local_info) {
    LOG(INFO) << "my_ip is " << butil::my_ip() << std::endl
              << "my_ip_cstr is " << butil::my_ip_cstr() << std::endl
              << "my_hostname is " << butil::my_hostname();
}

TEST(EndPointTest, endpoint) {
    butil::EndPoint p1;
    ASSERT_EQ(butil::IP_ANY, p1.ip);
    ASSERT_EQ(0, p1.port);
    
    butil::EndPoint p2(butil::IP_NONE, -1);
    ASSERT_EQ(butil::IP_NONE, p2.ip);
    ASSERT_EQ(-1, p2.port);

    butil::EndPoint p3;
    ASSERT_EQ(-1, butil::str2endpoint(" 127.0.0.1:-1", &p3));
    ASSERT_EQ(-1, butil::str2endpoint(" 127.0.0.1:65536", &p3));
    ASSERT_EQ(0, butil::str2endpoint(" 127.0.0.1:65535", &p3));
    ASSERT_EQ(0, butil::str2endpoint(" 127.0.0.1:0", &p3));

    butil::EndPoint p4;
    ASSERT_EQ(0, butil::str2endpoint(" 127.0.0.1: 289 ", &p4));
    ASSERT_STREQ("127.0.0.1", butil::ip2str(p4.ip).c_str());
    ASSERT_EQ(289, p4.port);
    
    butil::EndPoint p5;
    ASSERT_EQ(-1, hostname2endpoint("localhost:-1", &p5));
    ASSERT_EQ(-1, hostname2endpoint("localhost:65536", &p5));
    ASSERT_EQ(0, hostname2endpoint("localhost:65535", &p5)) << berror();
    ASSERT_EQ(0, hostname2endpoint("localhost:0", &p5));

#ifdef BAIDU_INTERNAL
    butil::EndPoint p6;
    ASSERT_EQ(0, hostname2endpoint("tc-cm-et21.tc: 289 ", &p6));
    ASSERT_STREQ("10.23.249.73", butil::ip2str(p6.ip).c_str());
    ASSERT_EQ(289, p6.port);
#endif
}
TEST(EndPointTest, endpoint_reject_trailing_characters_after_port) {
    butil::EndPoint ep;

    // invalid: non-whitespace after port
    ASSERT_EQ(-1, butil::str2endpoint("127.0.0.1:8000a", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("127.0.0.1:8000#", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("127.0.0.1:8000abc", &ep));

    // valid: only whitespace after port
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8000 ", &ep));
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8000\t", &ep));
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8000\n", &ep));
}

TEST(EndPointTest, hash_table) {
    butil::hash_map<butil::EndPoint, int> m;
    butil::EndPoint ep1(butil::IP_ANY, 123);
    butil::EndPoint ep2(butil::IP_ANY, 456);
    ++m[ep1];
    ASSERT_TRUE(m.find(ep1) != m.end());
    ASSERT_EQ(1, m.find(ep1)->second);
    ASSERT_EQ(1u, m.size());

    ++m[ep1];
    ASSERT_TRUE(m.find(ep1) != m.end());
    ASSERT_EQ(2, m.find(ep1)->second);
    ASSERT_EQ(1u, m.size());

    ++m[ep2];
    ASSERT_TRUE(m.find(ep2) != m.end());
    ASSERT_EQ(1, m.find(ep2)->second);
    ASSERT_EQ(2u, m.size());
}

TEST(EndPointTest, flat_map) {
    butil::FlatMap<butil::EndPoint, int> m;
    ASSERT_EQ(0, m.init(1024));
    uint32_t port = 8088;

    butil::EndPoint ep1(butil::IP_ANY, port);
    butil::EndPoint ep2(butil::IP_ANY, port);
    ++m[ep1];
    ++m[ep2];
    ASSERT_EQ(1u, m.size());

    butil::ip_t ip_addr;
    butil::str2ip("10.10.10.10", &ip_addr);
    int ip = butil::ip2int(ip_addr);

    for (int i = 0; i < 1023; ++i) {
        butil::EndPoint ep(butil::int2ip(++ip), port);
        ++m[ep];
    }

    butil::BucketInfo info = m.bucket_info();
    LOG(INFO) << "bucket info max long=" << info.longest_length
        << " avg=" << info.average_length << std::endl;
    ASSERT_LT(info.longest_length, 32ul) << "detect hash collision and it's too large.";
}

void* server_proc(void* arg) {
    int listen_fd = (int64_t)arg;
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    int fd = accept(listen_fd, (sockaddr*)&ss, &len);
    return (void*)(int64_t)fd;
}

static void test_listen_connect(const std::string& server_addr, const std::string& exp_client_addr) {
    butil::EndPoint point;
    ASSERT_EQ(0, butil::str2endpoint(server_addr.c_str(), &point));
    ASSERT_EQ(server_addr, butil::endpoint2str(point).c_str());

    int listen_fd = butil::tcp_listen(point);
    ASSERT_GT(listen_fd, 0);
    if (point.port == 0) {
        ASSERT_EQ(0, butil::get_local_side(listen_fd, &point));
    }
    const std::string actual_server_addr = butil::endpoint2str(point).c_str();
    pthread_t pid;
    pthread_create(&pid, nullptr, server_proc, (void*)(int64_t)listen_fd);

    int fd = butil::tcp_connect(point, nullptr);
    ASSERT_GT(fd, 0);

    butil::EndPoint point2;
    ASSERT_EQ(0, butil::get_local_side(fd, &point2));

    std::string s = butil::endpoint2str(point2).c_str();
    if (butil::get_endpoint_type(point2) == AF_UNIX) {
        ASSERT_EQ(exp_client_addr, s);
    } else {
        ASSERT_EQ(exp_client_addr, s.substr(0, exp_client_addr.size()));
    }
    ASSERT_EQ(0, butil::get_remote_side(fd, &point2));
    ASSERT_EQ(actual_server_addr, butil::endpoint2str(point2).c_str());
    close(fd);

    void* ret = nullptr;
    pthread_join(pid, &ret);
    int server_fd = (int)(int64_t)ret;
    ASSERT_GT(server_fd, 0);
    close(server_fd);
    close(listen_fd);
}

static void test_parse_and_serialize(const std::string& instr, const std::string& outstr) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint(instr.c_str(), &ep));
    butil::EndPointStr s = butil::endpoint2str(ep);
    ASSERT_EQ(outstr, std::string(s.c_str()));
}

TEST(EndPointTest, ipv4) {
    test_listen_connect("127.0.0.1:0", "127.0.0.1:");
}

TEST(EndPointTest, ipv6) {
    // FIXME: test environ may not support ipv6
    // test_listen_connect("[::1]:8787", "[::1]:");

    test_parse_and_serialize("[::1]:8080", "[::1]:8080");
    test_parse_and_serialize("  [::1]:65535  ", "[::1]:65535");
    test_parse_and_serialize("  [2001:0db8:a001:0002:0003:0ab9:C0A8:0102]:65535  ",
                             "[2001:db8:a001:2:3:ab9:c0a8:102]:65535");

    butil::EndPoint ep;
    ASSERT_EQ(-1, butil::str2endpoint("[2001:db8:1:2:3:ab9:c0a8:102]", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[2001:db8:1:2:3:ab9:c0a8:102]#654321", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("ipv6:2001:db8:1:2:3:ab9:c0a8:102", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[::1", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[]:80", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[]", &ep));
    ASSERT_EQ(-1, butil::str2endpoint("[]:", &ep));
}

TEST(EndPointTest, unix_socket) {
    ::unlink("test.sock");
    test_listen_connect("unix:test.sock", "unix:");
    ::unlink("test.sock");

    butil::EndPoint point;
    ASSERT_EQ(-1, butil::str2endpoint("", &point));
    ASSERT_EQ(-1, butil::str2endpoint("a.sock", &point));
    ASSERT_EQ(-1, butil::str2endpoint("unix:", &point));
    ASSERT_EQ(-1, butil::str2endpoint(" unix: ", &point));
    ASSERT_EQ(0, butil::str2endpoint("unix://a.sock", 123, &point));
    ASSERT_EQ(std::string("unix://a.sock"), butil::endpoint2str(point).c_str());

    std::string long_path = "unix:";
    long_path.append(sizeof(sockaddr_un::sun_path) - 1, 'a');
    ASSERT_EQ(0, butil::str2endpoint(long_path.c_str(), &point));
    ASSERT_EQ(long_path, butil::endpoint2str(point).c_str());
    long_path.push_back('a');
    ASSERT_EQ(-1, butil::str2endpoint(long_path.c_str(), &point));
    char buf[128] = {0}; // braft use this size of buffer
    size_t ret = snprintf(buf, sizeof(buf), "%s:%d", butil::endpoint2str(point).c_str(), INT_MAX);
    ASSERT_LT(ret, sizeof(buf) - 1);
}

TEST(EndPointTest, original_endpoint) {
    butil::EndPoint ep;
    ASSERT_FALSE(ExtendedEndPoint::is_extended(ep));
    ASSERT_EQ(nullptr, ExtendedEndPoint::address(ep));

    ASSERT_EQ(0, butil::str2endpoint("1.2.3.4:5678", &ep));
    ASSERT_FALSE(ExtendedEndPoint::is_extended(ep));
    ASSERT_EQ(nullptr, ExtendedEndPoint::address(ep));

    // ctor & dtor
    {
        butil::EndPoint ep2(ep);
        ASSERT_FALSE(ExtendedEndPoint::is_extended(ep));
        ASSERT_EQ(ep.ip, ep2.ip);
        ASSERT_EQ(ep.port, ep2.port);
    }

    // assign
    butil::EndPoint ep2;
    ep2 = ep;
    ASSERT_EQ(ep.ip, ep2.ip);
    ASSERT_EQ(ep.port, ep2.port);
}

TEST(EndPointTest, extended_endpoint) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("unix:sock.file", &ep));
    ASSERT_TRUE(ExtendedEndPoint::is_extended(ep));
    ExtendedEndPoint* eep = ExtendedEndPoint::address(ep);
    ASSERT_TRUE(eep);
    ASSERT_EQ(AF_UNIX, eep->family());
    ASSERT_EQ(1, eep->_ref_count.load());

    // copy ctor & dtor
    {
        butil::EndPoint tmp(ep);
        ASSERT_EQ(2, eep->_ref_count.load());
        ASSERT_EQ(eep, ExtendedEndPoint::address(tmp));
        ASSERT_EQ(eep, ExtendedEndPoint::address(ep));
    }
    ASSERT_EQ(1, eep->_ref_count.load());

    butil::EndPoint ep2;

    // extended endpoint assigns to original endpoint
    ep2 = ep;
    ASSERT_EQ(2, eep->_ref_count.load());
    ASSERT_EQ(eep, ExtendedEndPoint::address(ep2));

    // original endpoint assigns to extended endpoint
    ep2 = butil::EndPoint();
    ASSERT_EQ(1, eep->_ref_count.load());
    ASSERT_FALSE(ExtendedEndPoint::is_extended(ep2));

    // extended endpoint assigns to extended endpoint
    ASSERT_EQ(0, butil::str2endpoint("[::1]:2233", &ep2));
    ExtendedEndPoint* eep2 = ExtendedEndPoint::address(ep2);
    ASSERT_TRUE(eep2);
    ep2 = ep;
    // eep2 has been returned to resource pool, but we can still access it here unsafely.
    ASSERT_EQ(0, eep2->_ref_count.load());
    ASSERT_EQ(AF_UNSPEC, eep2->family());
    ASSERT_EQ(2, eep->_ref_count.load());
    ASSERT_EQ(eep, ExtendedEndPoint::address(ep));
    ASSERT_EQ(eep, ExtendedEndPoint::address(ep2));

    ASSERT_EQ(0, str2endpoint("[::1]:2233", &ep2));
    ASSERT_EQ(1, eep->_ref_count.load());
    eep2 = ExtendedEndPoint::address(ep2);
    ASSERT_NE(eep, eep2);
    ASSERT_EQ(1, eep2->_ref_count.load());
}

TEST(EndPointTest, failed_parse_leaves_endpoint_untouched) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("unix:forged.sock", &ep));
    ExtendedEndPoint* eep = ExtendedEndPoint::address(ep);
    ASSERT_TRUE(eep);
    ASSERT_EQ(1, eep->_ref_count.load());

    // The ip field of an extended endpoint holds its resource id, so this
    // string names `ep' with the port that marks extended endpoints.
    char forged[64];
    snprintf(forged, sizeof(forged), "%s:%d",
             butil::ip2str(ep.ip).c_str(), butil::details::EXTENDED_ENDPOINT_PORT);
    {
        butil::EndPoint tmp;
        ASSERT_EQ(-1, butil::str2endpoint(forged, &tmp));
        ASSERT_FALSE(ExtendedEndPoint::is_extended(tmp));
        ASSERT_EQ(-1, butil::hostname2endpoint(forged, &tmp));
        ASSERT_FALSE(ExtendedEndPoint::is_extended(tmp));
        // Callers fall back to another address after a failed parse.
        tmp = butil::EndPoint(butil::IP_ANY, 80);
    }
    ASSERT_EQ(1, eep->_ref_count.load());

    // An out-of-range port must not be narrowed to int before the range check.
    butil::EndPoint ep2;
    ASSERT_EQ(-1, butil::str2endpoint("127.0.0.1:4294967377", &ep2));
    ASSERT_EQ(-1, butil::hostname2endpoint("127.0.0.1:4294967377", &ep2));
    ASSERT_EQ(butil::IP_ANY, ep2.ip);
    ASSERT_EQ(0, ep2.port);
}

TEST(EndPointTest, endpoint_compare) {
    butil::EndPoint ep1, ep2, ep3;

    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8080", &ep1));
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8080", &ep2));
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.3:8080", &ep3));
    ASSERT_EQ(ep1, ep2);
    ASSERT_NE(ep1, ep3);

    ASSERT_EQ(0, butil::str2endpoint("unix:sock1.file", &ep1));
    ASSERT_EQ(0, butil::str2endpoint("unix:sock1.file", &ep2));
    ASSERT_EQ(0, butil::str2endpoint("unix:sock3.file", &ep3));
    ASSERT_EQ(ep1, ep2);
    ASSERT_NE(ep1, ep3);

    ASSERT_EQ(0, butil::str2endpoint("[::1]:2233", &ep1));
    ASSERT_EQ(0, butil::str2endpoint("[::1]:2233", &ep2));
    ASSERT_EQ(0, butil::str2endpoint("[::3]:2233", &ep3)); 
    ASSERT_EQ(ep1, ep2);
    ASSERT_NE(ep1, ep3);
}

TEST(EndPointTest, endpoint_sockaddr_conv_ipv4) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("1.2.3.4:8086", &ep));

    in_addr expected_in_addr;
    bzero(&expected_in_addr, sizeof(expected_in_addr));
    expected_in_addr.s_addr = 0x04030201u;

    sockaddr_storage ss;
    sockaddr_in* in4 = (sockaddr_in*) &ss;

    memset(&ss, 'a', sizeof(ss));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss));
    ASSERT_EQ(AF_INET, ss.ss_family);
    ASSERT_EQ(AF_INET, in4->sin_family);
    in_port_t port = htons(8086);
    ASSERT_EQ(port, in4->sin_port);
    ASSERT_EQ(0, memcmp(&in4->sin_addr, &expected_in_addr, sizeof(expected_in_addr)));

    sockaddr_storage ss2;
    socklen_t ss2_size = 0;
    memset(&ss2, 'b', sizeof(ss2));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss2, &ss2_size));
    ASSERT_EQ(ss2_size, sizeof(*in4));
    ASSERT_EQ(0, memcmp(&ss2, &ss, sizeof(ss)));

    butil::EndPoint ep2;
    ASSERT_EQ(0, butil::sockaddr2endpoint(&ss, sizeof(*in4), &ep2));
    ASSERT_EQ(ep2, ep);

    ASSERT_EQ(AF_INET, butil::get_endpoint_type(ep));
}

TEST(EndPointTest, endpoint_sockaddr_conv_ipv6) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("[::1]:8086", &ep));

    in6_addr expect_in6_addr;
    bzero(&expect_in6_addr, sizeof(expect_in6_addr));
    expect_in6_addr.s6_addr[15] = 1;

    sockaddr_storage ss;
    const sockaddr_in6* sa6 = (sockaddr_in6*) &ss;

    memset(&ss, 'a', sizeof(ss));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss));
    ASSERT_EQ(AF_INET6, ss.ss_family);
    ASSERT_EQ(AF_INET6, sa6->sin6_family);
    in_port_t port = htons(8086);
    ASSERT_EQ(port, sa6->sin6_port);
    ASSERT_EQ(0u, sa6->sin6_flowinfo);
    ASSERT_EQ(0, memcmp(&expect_in6_addr, &sa6->sin6_addr, sizeof(in6_addr)));
    ASSERT_EQ(0u, sa6->sin6_scope_id);

    sockaddr_storage ss2;
    socklen_t ss2_size = 0;
    memset(&ss2, 'b', sizeof(ss2));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss2, &ss2_size));
    ASSERT_EQ(ss2_size, sizeof(*sa6));
    ASSERT_EQ(0, memcmp(&ss2, &ss, sizeof(ss)));

    butil::EndPoint ep2;
    ASSERT_EQ(0, butil::sockaddr2endpoint(&ss, sizeof(*sa6), &ep2));
    ASSERT_STREQ("[::1]:8086", butil::endpoint2str(ep2).c_str());

    ASSERT_EQ(AF_INET6, butil::get_endpoint_type(ep));
}

TEST(EndPointTest, endpoint_sockaddr_conv_unix) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("unix:sock.file", &ep));

    sockaddr_storage ss;
    const sockaddr_un* un = (sockaddr_un*) &ss;

    memset(&ss, 'a', sizeof(ss));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss));
    ASSERT_EQ(AF_UNIX, ss.ss_family);
    ASSERT_EQ(AF_UNIX, un->sun_family);
    ASSERT_EQ(0, memcmp("sock.file", un->sun_path, 10));

    sockaddr_storage ss2;
    socklen_t ss2_size = 0;
    memset(&ss2, 'b', sizeof(ss2));
    ASSERT_EQ(0, butil::endpoint2sockaddr(ep, &ss2, &ss2_size));
    ASSERT_EQ(offsetof(struct sockaddr_un, sun_path) + strlen("sock.file") + 1, ss2_size);
    ASSERT_EQ(0, memcmp(&ss2, &ss, sizeof(ss)));

    butil::EndPoint ep2;
    ASSERT_EQ(0, butil::sockaddr2endpoint(&ss, sizeof(sa_family_t) + strlen(un->sun_path) + 1, &ep2));
    ASSERT_STREQ("unix:sock.file", butil::endpoint2str(ep2).c_str());

    ASSERT_EQ(AF_UNIX, butil::get_endpoint_type(ep));
}

void concurrent_proc(void* p) {
    for (int i = 0; i < 10000; ++i) {
        butil::EndPoint ep;
        std::string str("127.0.0.1:8080");
        ASSERT_EQ(0, butil::str2endpoint(str.c_str(), &ep));
        ASSERT_EQ(str, butil::endpoint2str(ep).c_str());

        str.assign("[::1]:8080");
        ASSERT_EQ(0, butil::str2endpoint(str.c_str(), &ep));
        ASSERT_EQ(str, butil::endpoint2str(ep).c_str());

        str.assign("unix:test.sock");
        ASSERT_EQ(0, butil::str2endpoint(str.c_str(), &ep));
        ASSERT_EQ(str, butil::endpoint2str(ep).c_str());
    }
    *(int*)p = 1;
}

TEST(EndPointTest, endpoint_concurrency) {
    const int T = 5;
    pthread_t tids[T];
    int rets[T] = {0};
    for (int i = 0; i < T; ++i) {
        pthread_create(&tids[i], nullptr, [](void* p) {
            concurrent_proc(p);
            return (void*)nullptr;
        }, &rets[i]);
    }
    for (int i = 0; i < T; ++i) {
        pthread_join(tids[i], nullptr);
        ASSERT_EQ(1, rets[i]);
    }
}

TEST(EndPointTest, tcp_connect) {
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:0", &ep));
    butil::fd_guard listener(butil::tcp_listen(ep));
    ASSERT_LE(0, listener) << "errno=" << errno;
    ASSERT_EQ(0, butil::get_local_side(listener, &ep));
    // The local TCP handshake completes without a concurrent accept thread.
    // Drain each connection before starting the next to keep the queue empty.
    {
        butil::fd_guard sockfd(butil::tcp_connect(ep, nullptr));
        ASSERT_LE(0, sockfd) << "errno=" << errno;
        butil::fd_guard accepted(accept(listener, nullptr, nullptr));
        ASSERT_LE(0, accepted) << "errno=" << errno;
    }
    {
        butil::fd_guard sockfd(butil::tcp_connect(ep, nullptr, 1000));
        ASSERT_LE(0, sockfd) << "errno=" << errno;
        butil::fd_guard accepted(accept(listener, nullptr, nullptr));
        ASSERT_LE(0, accepted) << "errno=" << errno;
    }

    sockaddr_storage serv_addr{};
    socklen_t serv_addr_size = 0;
    ASSERT_EQ(0, endpoint2sockaddr(ep, &serv_addr, &serv_addr_size));
    for (bool timed : {false, true}) {
        for (bool blocking : {false, true}) {
            butil::fd_guard sockfd(socket(serv_addr.ss_family, SOCK_STREAM, 0));
            ASSERT_LE(0, sockfd);
            if (!blocking) {
                ASSERT_EQ(0, butil::make_non_blocking(sockfd));
            }
            timespec deadline = butil::seconds_from_now(1);
            ASSERT_EQ(0, butil::pthread_timed_connect(
                sockfd, reinterpret_cast<sockaddr*>(&serv_addr), serv_addr_size,
                timed ? &deadline : nullptr)) << "errno=" << errno;
            ASSERT_EQ(blocking, butil::is_blocking(sockfd));
            ASSERT_EQ(0, butil::is_connected(sockfd));
            butil::fd_guard accepted(accept(listener, nullptr, nullptr));
            ASSERT_LE(0, accepted) << "errno=" << errno;
        }
    }
}

#if defined(OS_LINUX) || defined(OS_MACOSX)
int WaitForPipe(int fd, const timespec* abstime) {
#if defined(OS_LINUX)
    return butil::pthread_fd_wait(fd, EPOLLIN, abstime);
#else
    // Match the unsigned event encoding used by pthread_timed_connect.
    return butil::pthread_fd_wait(
        fd, static_cast<unsigned>(EVFILT_READ), abstime);
#endif
}

TEST(EndPointTest, fd_wait_timeout) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    butil::fd_guard reader(fds[0]);
    butil::fd_guard writer(fds[1]);
    // An empty pipe tests the deadline without TCP queue or network assumptions.
    timespec deadline = butil::milliseconds_from_now(300);
    ASSERT_EQ(-1, WaitForPipe(reader, &deadline));
    ASSERT_EQ(ETIMEDOUT, errno);
}

#if defined(OS_MACOSX)
TEST(EndPointTest, fd_wait_kqueue_events) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    butil::fd_guard reader(fds[0]);
    butil::fd_guard writer(fds[1]);
    timespec deadline = butil::seconds_from_now(5);
    ASSERT_EQ(0, butil::pthread_fd_wait(
        writer, static_cast<unsigned>(EVFILT_WRITE), &deadline));

    const char byte = 'x';
    ASSERT_EQ(1, write(writer, &byte, sizeof(byte)));
    ASSERT_EQ(0, butil::pthread_fd_wait(
        reader, static_cast<unsigned>(EVFILT_READ), &deadline));

    ASSERT_EQ(-1, butil::pthread_fd_wait(reader, 0u, &deadline));
    EXPECT_EQ(EINVAL, errno);
}
#endif

volatile sig_atomic_t g_sigurg_received = 0;

void RecordSigurg(int) {
    g_sigurg_received = 1;
}

struct FDWaitArgs {
    int fd;
    bool timed;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    int signal_mask_error = 0;
    int result = -1;
    int error = 0;
};

void* FDWaitThread(void* arg) {
    FDWaitArgs* args = static_cast<FDWaitArgs*>(arg);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGURG);
    args->signal_mask_error = pthread_sigmask(SIG_UNBLOCK, &signals, nullptr);
    timespec deadline = butil::seconds_from_now(5);
    args->started.store(true, std::memory_order_release);
    if (args->signal_mask_error == 0) {
        args->result = WaitForPipe(args->fd, args->timed ? &deadline : nullptr);
        if (args->result != 0) {
            args->error = errno;
        }
    }
    args->finished.store(true, std::memory_order_release);
    return nullptr;
}

void TestFDWaitWithSignals(bool timed) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    butil::fd_guard reader(fds[0]);
    butil::fd_guard writer(fds[1]);
    FDWaitArgs args;
    args.fd = reader;
    args.timed = timed;
    g_sigurg_received = 0;
    pthread_t tid;
    ASSERT_EQ(0, pthread_create(&tid, nullptr, FDWaitThread, &args));

    const int64_t deadline = butil::cpuwide_time_us() + 5000000L;
    while (!args.started.load(std::memory_order_acquire) &&
           butil::cpuwide_time_us() < deadline) {
        usleep(1000);
    }
    EXPECT_TRUE(args.started.load(std::memory_order_acquire));
    // Exercise waiting with repeated signals before making the pipe readable.
    // This checks signal delivery and the wait result, not an exact EINTR count.
    for (int i = 0; i < 10 &&
         !args.finished.load(std::memory_order_acquire) &&
         butil::cpuwide_time_us() < deadline; ++i) {
        const int rc = pthread_kill(tid, SIGURG);
        EXPECT_EQ(0, rc);
        if (rc != 0) {
            break;
        }
        usleep(1000);
    }
    EXPECT_FALSE(args.finished.load(std::memory_order_acquire));

    const char byte = 'x';
    EXPECT_EQ(1, write(writer, &byte, sizeof(byte)));
    // Closing the writer also wakes poll if an earlier expectation failed.
    writer.reset(-1);
    ASSERT_EQ(0, pthread_join(tid, nullptr));
    ASSERT_EQ(0, args.signal_mask_error) << "pthread_sigmask failed";
    // The SIGURG handler sets the flag; read it after joining the waiter.
    EXPECT_EQ(1, g_sigurg_received);
    EXPECT_EQ(0, args.result) << "errno=" << args.error;
}

TEST(EndPointTest, fd_wait_with_signals) {
    struct sigaction action{};
    struct sigaction old_action;
    action.sa_handler = RecordSigurg;
    sigemptyset(&action.sa_mask);
    ASSERT_EQ(0, sigaction(SIGURG, &action, &old_action));
    BRPC_SCOPE_EXIT {
        EXPECT_EQ(0, sigaction(SIGURG, &old_action, nullptr));
    };
    ASSERT_NO_FATAL_FAILURE(TestFDWaitWithSignals(false));
    ASSERT_NO_FATAL_FAILURE(TestFDWaitWithSignals(true));
}
#endif  // defined(OS_LINUX) || defined(OS_MACOSX)

} // end of namespace
