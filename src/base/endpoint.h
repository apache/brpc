// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved
//
// Wrappers of IP and port.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BRPC_BASE_ENDPOINT_H
#define BRPC_BASE_ENDPOINT_H

#include <netinet/in.h>                          // in_addr
#include <iostream>                              // std::ostream
#include "base/containers/hash_tables.h"         // hashing functions

namespace base {

// Type of an IP address
typedef struct in_addr ip_t;

static const ip_t IP_ANY = { INADDR_ANY };
static const ip_t IP_NONE = { INADDR_NONE };

// Convert |ip| to an integral
inline in_addr_t ip2int(ip_t ip) { return ip.s_addr; }

// Convert integral |ip_value| to an IP
inline ip_t int2ip(in_addr_t ip_value) {
    const ip_t ip = { ip_value };
    return ip;
}

// Convert string `ip_str' to ip_t *ip.
// `ip_str' is in IPv4 dotted-quad format: `127.0.0.1', `10.23.249.73' ...
// Returns 0 on success, -1 otherwise.
int str2ip(const char* ip_str, ip_t* ip);

struct IPStr {
    const char* c_str() const { return _buf; }
    char _buf[INET_ADDRSTRLEN];
};

// Convert IP to c-style string. Notice that you can serialize ip_t to
// std::ostream directly. Use this function when you don't have streaming log.
// Example: printf("ip=%s\n", ip2str(some_ip).c_str());
IPStr ip2str(ip_t ip);

// Convert `hostname' to ip_t *ip. If `hostname' is NULL, use hostname
// of this machine.
// `hostname' is typically in this form: `tc-cm-et21.tc' `db-cos-dev.db01' ...
// Returns 0 on success, -1 otherwise.
int hostname2ip(const char* hostname, ip_t* ip);

// Convert `ip' to `hostname'.
// Returns 0 on success, -1 otherwise and errno is set.
int ip2hostname(ip_t ip, char* hostname, size_t hostname_len);
int ip2hostname(ip_t ip, std::string* hostname);

// Hostname of this machine, "" on error.
// NOTE: This function caches result on first call.
const char* my_hostname();

// IP of this machine, IP_ANY on error.
// NOTE: This function caches result on first call.
ip_t my_ip();
// String form.
const char* my_ip_cstr();

// ipv4 + port
struct EndPoint {
    EndPoint() : ip(IP_ANY), port(0) {}
    EndPoint(ip_t ip2, int port2) : ip(ip2), port(port2) {}
    explicit EndPoint(const sockaddr_in& in)
        : ip(in.sin_addr), port(ntohs(in.sin_port)) {}
    
    ip_t ip;
    int port;
};

struct EndPointStr {
    const char* c_str() const { return _buf; }
    char _buf[INET_ADDRSTRLEN + 16];
};

// Convert EndPoint to c-style string. Notice that you can serialize 
// EndPoint to std::ostream directly. Use this function when you don't 
// have streaming log.
// Example: printf("point=%s\n", endpoint2str(point).c_str());
EndPointStr endpoint2str(const EndPoint&);

// Convert string `ip_and_port_str' to a EndPoint *point.
// Returns 0 on success, -1 otherwise.
int str2endpoint(const char* ip_and_port_str, EndPoint* point);
int str2endpoint(const char* ip_str, int port, EndPoint* point);

// Convert `hostname_and_port_str' to a EndPoint *point.
// Returns 0 on success, -1 otherwise.
int hostname2endpoint(const char* ip_and_port_str, EndPoint* point);
int hostname2endpoint(const char* name_str, int port, EndPoint* point);

// Convert `endpoint' to `hostname'.
// Returns 0 on success, -1 otherwise and errno is set.
int endpoint2hostname(const EndPoint& point, char* hostname, size_t hostname_len);
int endpoint2hostname(const EndPoint& point, std::string* host);

// Create a TCP socket and connect it to `server'. Write port of this side
// into `self_port' if it's not NULL.
// Returns the socket descriptor, -1 otherwise and errno is set.
int tcp_connect(EndPoint server, int* self_port);

// Create and listen to a TCP socket bound with `ip_and_port'. If `reuse_addr'
// is true, ports in TIME_WAIT will be bound as well.
// Returns the socket descriptor, -1 otherwise and errno is set.
int tcp_listen(EndPoint ip_and_port, bool reuse_addr);

// Get the local end of a socket connection
int get_local_side(int fd, EndPoint *out);

// Get the other end of a socket connection
int get_remote_side(int fd, EndPoint *out);

}  // namespace base

// Since ip_t is defined from in_addr which is globally defined, due to ADL
// we have to put overloaded operators globally as well.
inline bool operator<(base::ip_t lhs, base::ip_t rhs) {
    return base::ip2int(lhs) < base::ip2int(rhs);
}
inline bool operator>(base::ip_t lhs, base::ip_t rhs) {
    return rhs < lhs;
}
inline bool operator>=(base::ip_t lhs, base::ip_t rhs) {
    return !(lhs < rhs);
}
inline bool operator<=(base::ip_t lhs, base::ip_t rhs) {
    return !(rhs < lhs); 
}
inline bool operator==(base::ip_t lhs, base::ip_t rhs) {
    return base::ip2int(lhs) == base::ip2int(rhs);
}
inline bool operator!=(base::ip_t lhs, base::ip_t rhs) {
    return !(lhs == rhs);
}

inline std::ostream& operator<<(std::ostream& os, const base::IPStr& ip_str) {
    return os << ip_str.c_str();
}
inline std::ostream& operator<<(std::ostream& os, base::ip_t ip) {
    return os << base::ip2str(ip);
}

namespace base {
// Overload operators for EndPoint in the same namespace due to ADL.
inline bool operator<(EndPoint p1, EndPoint p2) {
    return (p1.ip != p2.ip) ? (p1.ip < p2.ip) : (p1.port < p2.port);
}
inline bool operator>(EndPoint p1, EndPoint p2) {
    return p2 < p1;
}
inline bool operator<=(EndPoint p1, EndPoint p2) { 
    return !(p2 < p1); 
}
inline bool operator>=(EndPoint p1, EndPoint p2) { 
    return !(p1 < p2); 
}
inline bool operator==(EndPoint p1, EndPoint p2) {
    return p1.ip == p2.ip && p1.port == p2.port;
}
inline bool operator!=(EndPoint p1, EndPoint p2) {
    return !(p1 == p2);
}

inline std::ostream& operator<<(std::ostream& os, const EndPoint& ep) {
    return os << ep.ip << ':' << ep.port;
}
inline std::ostream& operator<<(std::ostream& os, const EndPointStr& ep_str) {
    return os << ep_str.c_str();
}

}  // namespace base


namespace BASE_HASH_NAMESPACE {

// Implement methods for hashing a pair of integers, so they can be used as
// keys in STL containers.

#if defined(COMPILER_MSVC)

inline std::size_t hash_value(const base::EndPoint& ep) {
    return base::HashPair(base::ip2int(ep.ip), ep.port);
}

#elif defined(COMPILER_GCC)
template <>
struct hash<base::EndPoint> {
    std::size_t operator()(const base::EndPoint& ep) const {
        return base::HashPair(base::ip2int(ep.ip), ep.port);
    }
};

#else
#error define hash<EndPoint> for your compiler
#endif  // COMPILER

}

#endif  // BRPC_BASE_ENDPOINT_H
