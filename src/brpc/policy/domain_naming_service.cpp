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
#include "butil/build_config.h"                       // OS_MACOSX
#include <netdb.h>                                    // gethostbyname_r
#include <stdlib.h>                                   // strtol
#include <string>                                     // std::string
#include "bthread/bthread.h"
#include "brpc/log.h"
#include "brpc/policy/domain_naming_service.h"

DEFINE_bool(dns_support_ipv6, false, "Resolve DNS by IPV6 address first");

namespace brpc {
namespace policy {

DomainNamingService::DomainNamingService(int default_port)
    : _aux_buf_len(0)
    , _default_port(default_port) {}

int DomainNamingService::GetServers(const char* dns_name,
                                    std::vector<ServerNode>* servers) {
    servers->clear();
    if (!dns_name) {
        LOG(ERROR) << "dns_name is NULL";
        return -1;
    }

    // Should be enough to hold host name
    char buf[256];
    size_t i = 0;
    for (; i < sizeof(buf) - 1 && dns_name[i] != '\0'
             && dns_name[i] != ':' && dns_name[i] != '/'; ++i) {
        buf[i] = dns_name[i];
    }
    if (i == sizeof(buf) - 1) {
        LOG(ERROR) << "dns_name=`" << dns_name << "' is too long";
        return -1;
    }
    
    buf[i] = '\0';
    int port = _default_port;
    if (dns_name[i] == ':') {
        ++i;
        char* end = NULL;
        port = strtol(dns_name + i, &end, 10);
        if (end == dns_name + i) {
            LOG(ERROR) << "No port after colon in `" << dns_name << '\'';
            return -1;
        } else if (*end != '\0') {
            if (*end != '/') {
                LOG(ERROR) << "Invalid content=`" << end << "' after port="
                           << port << " in `" << dns_name << '\'';
                return -1;
            }
            // Drop path and other stuff.
            RPC_VLOG << "Drop content=`" << end << "' after port=" << port
                     << " in `" << dns_name << '\'';
            // NOTE: Don't ever change *end which is const.
        }
    }
    if (port < 0 || port > 65535) {
        LOG(ERROR) << "Invalid port=" << port << " in `" << dns_name << '\'';
        return -1;
    }

    if (FLAGS_dns_support_ipv6) {
        struct addrinfo hints;
        struct addrinfo* addrResult;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_DGRAM;
        char portBuf[16];
        snprintf(portBuf, arraysize(portBuf), "%d", port);
        auto ret = getaddrinfo(buf, portBuf, &hints, &addrResult);
        if (!ret) {
            for(auto rp = addrResult; rp != NULL; rp = rp->ai_next) {
                butil::EndPoint point;
                auto ret = butil::sockaddr2endpoint((struct sockaddr_storage*)rp->ai_addr, rp->ai_addrlen, &point);
                if(!ret) {
                    servers->push_back(ServerNode(point, std::string()));
                }
            }

            freeaddrinfo(addrResult);
            return 0;
        } else {
            LOG(WARNING) << "Can't resolve `" << buf << "for ipv6, fallback to ipv4";
            // fallback to ipv4
        }
        
    }

#if defined(OS_MACOSX)
    _aux_buf_len = 0; // suppress unused warning
    // gethostbyname on MAC is thread-safe (with current usage) since the
    // returned hostent is TLS. Check following link for the ref:
    // https://lists.apple.com/archives/darwin-dev/2006/May/msg00008.html
    struct hostent* result = gethostbyname(buf);
    if (result == NULL) {
        LOG(WARNING) << "result of gethostbyname is NULL";
        return -1;
    }
#else
    if (_aux_buf == NULL) {
        _aux_buf_len = 1024;
        _aux_buf.reset(new char[_aux_buf_len]);
    }
    int ret = 0;
    int error = 0;
    struct hostent ent;
    struct hostent* result = NULL;
    do {
        result = NULL;
        error = 0;
        ret = gethostbyname_r(buf, &ent, _aux_buf.get(), _aux_buf_len,
                              &result, &error);
        if (ret != ERANGE) { // _aux_buf is not long enough
            break;
        }
        _aux_buf_len *= 2;
        _aux_buf.reset(new char[_aux_buf_len]);
        RPC_VLOG << "Resized _aux_buf to " << _aux_buf_len
                 << ", dns_name=" << dns_name;
    } while (1);
    if (ret != 0) {
        // `hstrerror' is thread safe under linux
        LOG(WARNING) << "Can't resolve `" << buf << "', return=`" << berror(ret)
                     << "' herror=`" << hstrerror(error) << '\'';
        return -1;
    }
    if (result == NULL) {
        LOG(WARNING) << "result of gethostbyname_r is NULL";
        return -1;
    }
#endif

    //TODO add protocols other than IPv4 supports
    butil::EndPoint point;
    point.port = port;
    for (int i = 0; result->h_addr_list[i] != NULL; ++i) {
        if (result->h_addrtype == AF_INET) {
            // Only fetch IPv4 addresses
            bcopy(result->h_addr_list[i], &point.ip, result->h_length);
            servers->push_back(ServerNode(point, std::string()));
        } else {
            LOG(WARNING) << "Found address of unsupported protocol="
                         << result->h_addrtype;
        }
    }
    return 0;
}

void DomainNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "http";
    return;
}

NamingService* DomainNamingService::New() const {
    return new DomainNamingService(_default_port);
}

void DomainNamingService::Destroy() {
    delete this;
}

}  // namespace policy
} // namespace brpc
