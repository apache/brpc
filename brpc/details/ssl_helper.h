// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Jul 21 15:04:37 2015

#ifndef BRPC_SSL_HELPER_H
#define BRPC_SSL_HELPER_H

#include <string.h>
#include <openssl/ssl.h>
// For some versions of openssl, SSL_* are defined inside this header
#include <openssl/ossl_typ.h>
#include "brpc/server.h"               // SSLOptions
#include "brpc/socket_id.h"            // SocketId


namespace brpc {

enum SSLState {
    SSL_UNKNOWN = 0,
    SSL_OFF = 1,                // Not an SSL connection
    SSL_CONNECTING = 2,         // During SSL handshake
    SSL_CONNECTED = 3,          // SSL handshake completed
};

struct SSLError {
    explicit SSLError(unsigned long e) : error(e) { }
    unsigned long error;
};
std::ostream& operator<<(std::ostream& os, const SSLError&);
std::ostream& operator<<(std::ostream& os, const CertInfo&);

const char* SSLStateToString(SSLState s);

// Initialize locks and callbacks to make SSL work under multi-threaded
// environment. Return 0 on success, -1 otherwise
int SSLThreadInit();

// Initialize global Diffie-Hellman parameters used for DH key exchanges
// Return 0 on success, -1 otherwise
int SSLDHInit();

// Create a new SSL_CTX using `certificate_file' and `private_key_file'
// and then set the right options onto it according `options'. Finally,
// extract hostnames from CN/subject fields into `hostnames'
SSL_CTX* CreateSSLContext(const std::string& certificate_file,
                          const std::string& private_key_file,
                          const SSLOptions& options,
                          std::vector<std::string>* hostnames);

// Create a new SSL (per connection object) using configurations in `ctx'.
// Set the required `fd' and mode. `id' will be set into SSL as app data.
SSL* CreateSSLSession(SSL_CTX* ctx, SocketId id, int fd, bool server_mode);

// Judge whether the underlying channel of `fd' is using SSL
// If the return value is SSL_UNKNOWN, `error_code' will be
// set to indicate the reason (0 for EOF)
SSLState DetectSSLState(int fd, int* error_code);

} // namespace brpc


#endif // BRPC_SSL_HELPER_H

