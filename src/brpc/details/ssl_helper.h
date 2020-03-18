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


#ifndef BRPC_SSL_HELPER_H
#define BRPC_SSL_HELPER_H

#include <string.h>
#ifndef USE_MESALINK
#include <openssl/ssl.h>
// For some versions of openssl, SSL_* are defined inside this header
#include <openssl/ossl_typ.h>
#else
#include <mesalink/openssl/ssl.h>
#include <mesalink/openssl/err.h>
#include <mesalink/openssl/x509.h>
#endif
#include "brpc/socket_id.h"            // SocketId
#include "brpc/ssl_options.h"          // ServerSSLOptions

namespace brpc {

enum SSLState {
    SSL_UNKNOWN = 0,
    SSL_OFF = 1,                // Not an SSL connection
    SSL_CONNECTING = 2,         // During SSL handshake
    SSL_CONNECTED = 3,          // SSL handshake completed
};

enum SSLProtocol {
    SSLv3 = 1 << 0,
    TLSv1 = 1 << 1,
    TLSv1_1 = 1 << 2,
    TLSv1_2 = 1 << 3,
};

struct FreeSSLCTX {
    inline void operator()(SSL_CTX* ctx) const {
        if (ctx != NULL) {
            SSL_CTX_free(ctx);
        }
    }
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

// Create a new SSL_CTX in client mode and
// set the right options according `options'
SSL_CTX* CreateClientSSLContext(const ChannelSSLOptions& options);

// Create a new SSL_CTX in server mode using `certificate_file'
// and `private_key_file' and then set the right options onto it
// according `options'. Finally, extract hostnames from CN/subject
// fields into `hostnames'
SSL_CTX* CreateServerSSLContext(const std::string& certificate_file,
                                const std::string& private_key_file,
                                const ServerSSLOptions& options,
                                std::vector<std::string>* hostnames);

// Create a new SSL (per connection object) using configurations in `ctx'.
// Set the required `fd' and mode. `id' will be set into SSL as app data.
SSL* CreateSSLSession(SSL_CTX* ctx, SocketId id, int fd, bool server_mode);

// Add a buffer layer of BIO in front of the socket fd layer,
// which can reduce the total number of calls to system read/write
void AddBIOBuffer(SSL* ssl, int fd, int bufsize);

// Judge whether the underlying channel of `fd' is using SSL
// If the return value is SSL_UNKNOWN, `error_code' will be
// set to indicate the reason (0 for EOF)
SSLState DetectSSLState(int fd, int* error_code);

void Print(std::ostream& os, SSL* ssl, const char* sep);
void Print(std::ostream& os, X509* cert, const char* sep);

} // namespace brpc

#endif // BRPC_SSL_HELPER_H
