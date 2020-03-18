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


#ifdef USE_MESALINK

#include <sys/socket.h>                // recv
#include <mesalink/openssl/ssl.h>
#include <mesalink/openssl/err.h>
#include <mesalink/openssl/x509.h>
#include <mesalink/openssl/bio.h>
#include <mesalink/openssl/evp.h>
#include <mesalink/openssl/pem.h>
#include "butil/unique_ptr.h"
#include "butil/logging.h"
#include "butil/string_splitter.h"
#include "brpc/socket.h"
#include "brpc/ssl_options.h"
#include "brpc/details/ssl_helper.h"

namespace brpc {

static const char* const PEM_START = "-----BEGIN";

static bool IsPemString(const std::string& input) {
    for (const char* s = input.c_str(); *s != '\0'; ++s) {
        if (*s != '\n') {
            return strncmp(s, PEM_START, strlen(PEM_START)) == 0;
        } 
    }
    return false;
}

const char* SSLStateToString(SSLState s) {
    switch (s) {
    case SSL_UNKNOWN:
        return "SSL_UNKNOWN";
    case SSL_OFF:
        return "SSL_OFF";
    case SSL_CONNECTING:
        return "SSL_CONNECTING";
    case SSL_CONNECTED:
        return "SSL_CONNECTED";
    }
    return "Bad SSLState";
}

static int ParseSSLProtocols(const std::string& str_protocol) {
    int protocol_flag = 0;
    butil::StringSplitter sp(str_protocol.data(),
                             str_protocol.data() + str_protocol.size(), ',');
    for (; sp; ++sp) {
        butil::StringPiece protocol(sp.field(), sp.length());
        protocol.trim_spaces();
        if (strncasecmp(protocol.data(), "SSLv3", protocol.size()) == 0
            || strncasecmp(protocol.data(), "TLSv1", protocol.size()) == 0
            || strncasecmp(protocol.data(), "TLSv1.1", protocol.size()) == 0) {
            LOG(WARNING) << "Ignored insecure SSL/TLS protocol=" << protocol;
        } else if (strncasecmp(protocol.data(), "TLSv1.2", protocol.size()) == 0) {
            protocol_flag |= TLSv1_2;
        } else {
            LOG(ERROR) << "Unknown SSL protocol=" << protocol;
            return -1;
        }
    }
    return protocol_flag;
}

std::ostream& operator<<(std::ostream& os, const SSLError& ssl) {
    char buf[128];  // Should be enough
    ERR_error_string_n(ssl.error, buf, sizeof(buf));
    return os << buf;
}

std::ostream& operator<<(std::ostream& os, const CertInfo& cert) {
    os << "certificate[";
    if (IsPemString(cert.certificate)) {
        size_t pos = cert.certificate.find('\n');
        if (pos == std::string::npos) {
            pos = 0;
        } else {
            pos++;
        }
        os << cert.certificate.substr(pos, 16) << "...";
    } else {
        os << cert.certificate;
    } 

    os << "] private-key[";
    if (IsPemString(cert.private_key)) {
        size_t pos = cert.private_key.find('\n');
        if (pos == std::string::npos) {
            pos = 0;
        } else {
            pos++;
        }
        os << cert.private_key.substr(pos, 16) << "...";
    } else {
        os << cert.private_key;
    }
    os << "]";
    return os;
}

void ExtractHostnames(X509* x, std::vector<std::string>* hostnames) {
    STACK_OF(X509_NAME)* names = (STACK_OF(X509_NAME)*)
            X509_get_alt_subject_names(x);
    if (names) {
        for (int i = 0; i < sk_X509_NAME_num(names); i++) {
            char buf[255] = {0};
            X509_NAME* name = sk_X509_NAME_value(names, i);
            if (X509_NAME_oneline(name, buf, 255)) {
                std::string hostname(buf);
                hostnames->push_back(hostname);
            }
        }
        sk_X509_NAME_free(names);
    }
}

struct FreeSSL {
    inline void operator()(SSL* ssl) const {
        if (ssl != NULL) {
            SSL_free(ssl);
        }
    }
};

struct FreeBIO {
    inline void operator()(BIO* io) const {
        if (io != NULL) {
            BIO_free(io);
        }
    }
};

struct FreeX509 {
    inline void operator()(X509* x) const {
        if (x != NULL) {
            X509_free(x);
        }
    }
};

struct FreeEVPKEY {
    inline void operator()(EVP_PKEY* k) const {
        if (k != NULL) {
            EVP_PKEY_free(k);
        }
    }
};

static int LoadCertificate(SSL_CTX* ctx,
                           const std::string& certificate,
                           const std::string& private_key,
                           std::vector<std::string>* hostnames) {
    // Load the private key
    if (IsPemString(private_key)) {
        std::unique_ptr<BIO, FreeBIO> kbio(
            BIO_new_mem_buf((void*)private_key.c_str(), -1));
        std::unique_ptr<EVP_PKEY, FreeEVPKEY> key(
            PEM_read_bio_PrivateKey(kbio.get(), NULL, 0, NULL));
        if (SSL_CTX_use_PrivateKey(ctx, key.get()) != 1) {
            LOG(ERROR) << "Fail to load " << private_key << ": "
                       << SSLError(ERR_get_error());
            return -1;
        }
    } else {
        if (SSL_CTX_use_PrivateKey_file(
                ctx, private_key.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG(ERROR) << "Fail to load " << private_key << ": "
                       << SSLError(ERR_get_error());
            return -1;
        }
    }

    // Open & Read certificate
    std::unique_ptr<BIO, FreeBIO> cbio;
    if (IsPemString(certificate)) {
        cbio.reset(BIO_new_mem_buf((void*)certificate.c_str(), -1));
    } else {
        cbio.reset(BIO_new(BIO_s_file()));
        if (BIO_read_filename(cbio.get(), certificate.c_str()) <= 0) {
            LOG(ERROR) << "Fail to read " << certificate << ": "
                       << SSLError(ERR_get_error());
            return -1;
        }
    }
    std::unique_ptr<X509, FreeX509> x(
        PEM_read_bio_X509(cbio.get(), NULL, 0, NULL));
    if (!x) {
        LOG(ERROR) << "Fail to parse " << certificate << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }
    
    // Load the main certficate
    if (SSL_CTX_use_certificate(ctx, x.get()) != 1) {
        LOG(ERROR) << "Fail to load " << certificate << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }

    // Load the certificate chain
    //SSL_CTX_clear_chain_certs(ctx);
    X509* ca = NULL;
    while ((ca = PEM_read_bio_X509(cbio.get(), NULL, 0, NULL))) {
        if (SSL_CTX_add_extra_chain_cert(ctx, ca) != 1) {
            LOG(ERROR) << "Fail to load chain certificate in "
                       << certificate << ": " << SSLError(ERR_get_error());
            X509_free(ca);
            return -1;
        }
    }
    ERR_clear_error();

    // Validate certificate and private key 
    if (SSL_CTX_check_private_key(ctx) != 1) {
        LOG(ERROR) << "Fail to verify " << private_key << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }

    return 0;
}

static int SetSSLOptions(SSL_CTX* ctx, const std::string& ciphers,
                         int protocols, const VerifyOptions& verify) {
    if (verify.verify_depth > 0) {
        std::string cafile = verify.ca_file_path;
        if (!cafile.empty()) {
            if (SSL_CTX_load_verify_locations(ctx, cafile.c_str(), NULL) == 0) {
                LOG(ERROR) << "Fail to load CA file " << cafile
                           << ": " << SSLError(ERR_get_error());
                return -1;
            }
        }
        SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER
                                 | SSL_VERIFY_FAIL_IF_NO_PEER_CERT), NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    return 0;
}

SSL_CTX* CreateClientSSLContext(const ChannelSSLOptions& options) {
    std::unique_ptr<SSL_CTX, FreeSSLCTX> ssl_ctx(
        SSL_CTX_new(TLSv1_2_client_method()));
    if (!ssl_ctx) {
        LOG(ERROR) << "Fail to new SSL_CTX: " << SSLError(ERR_get_error());
        return NULL;
    }

    if (!options.client_cert.certificate.empty()
        && LoadCertificate(ssl_ctx.get(),
                           options.client_cert.certificate,
                           options.client_cert.private_key, NULL) != 0) {
        return NULL;
    }

    int protocols = ParseSSLProtocols(options.protocols);
    if (protocols < 0
        || SetSSLOptions(ssl_ctx.get(), options.ciphers,
                         protocols, options.verify) != 0) {
        return NULL;
    }

    SSL_CTX_set_session_cache_mode(ssl_ctx.get(), SSL_SESS_CACHE_CLIENT);
    return ssl_ctx.release();
}

SSL_CTX* CreateServerSSLContext(const std::string& certificate,
                                const std::string& private_key,
                                const ServerSSLOptions& options,
                                std::vector<std::string>* hostnames) {
    std::unique_ptr<SSL_CTX, FreeSSLCTX> ssl_ctx(
        SSL_CTX_new(TLSv1_2_server_method()));
    if (!ssl_ctx) {
        LOG(ERROR) << "Fail to new SSL_CTX: " << SSLError(ERR_get_error());
        return NULL;
    }

    if (LoadCertificate(ssl_ctx.get(), certificate,
                        private_key, hostnames) != 0) {
        return NULL;
    }

    int protocols = TLSv1 | TLSv1_1 | TLSv1_2;
    if (!options.disable_ssl3) {
        protocols |= SSLv3;
    }
    if (SetSSLOptions(ssl_ctx.get(), options.ciphers,
                      protocols, options.verify) != 0) {
        return NULL;
    }

    /* SSL_CTX_set_timeout(ssl_ctx.get(), options.session_lifetime_s); */
    SSL_CTX_sess_set_cache_size(ssl_ctx.get(), options.session_cache_size);

    return ssl_ctx.release();
}

SSL* CreateSSLSession(SSL_CTX* ctx, SocketId id, int fd, bool server_mode) {
    if (ctx == NULL) {
        LOG(WARNING) << "Lack SSL_ctx to create an SSL session";
        return NULL;
    }
    SSL* ssl = SSL_new(ctx);
    if (ssl == NULL) {
        LOG(ERROR) << "Fail to SSL_new: " << SSLError(ERR_get_error());
        return NULL;
    }
    if (SSL_set_fd(ssl, fd) != 1) {
        LOG(ERROR) << "Fail to SSL_set_fd: " << SSLError(ERR_get_error());
        SSL_free(ssl);
        return NULL;
    }

    if (server_mode) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }

    return ssl;
}

void AddBIOBuffer(SSL* ssl, int fd, int bufsize) {
    // MesaLink uses buffered IO internally
}

SSLState DetectSSLState(int fd, int* error_code) {
    // Peek the first few bytes inside socket to detect whether
    // it's an SSL connection. If it is, create an SSL session
    // which will be used to read/write after

    // Header format of SSLv2
    // +-----------+------+-----
    // | 2B header | 0x01 | etc.
    // +-----------+------+-----
    // The first bit of header is always 1, with the following
    // 15 bits are the length of data

    // Header format of SSLv3 or TLSv1.0, 1.1, 1.2
    // +------+------------+-----------+------+-----
    // | 0x16 | 2B version | 2B length | 0x01 | etc.
    // +------+------------+-----------+------+-----
    char header[6];
    const ssize_t nr = recv(fd, header, sizeof(header), MSG_PEEK);
    if (nr < (ssize_t)sizeof(header)) {
        if (nr < 0) {
            if (errno == ENOTSOCK) {
                return SSL_OFF;
            }
            *error_code = errno;   // Including EAGAIN and EINTR
        } else if (nr == 0) {      // EOF
            *error_code = 0;
        } else {                   // Not enough data, need retry
            *error_code = EAGAIN;
        }
        return SSL_UNKNOWN;
    }
    
    if ((header[0] == 0x16 && header[5] == 0x01) // SSLv3 or TLSv1.0, 1.1, 1.2
        || ((header[0] & 0x80) == 0x80 && header[2] == 0x01)) {  // SSLv2
        return SSL_CONNECTING;
    } else {
        return SSL_OFF;
    }
}

int SSLThreadInit() {
    return 0;
}

int SSLDHInit() {
    return 0;
}

void Print(std::ostream& os, SSL* ssl, const char* sep) {
    os << "cipher=" << SSL_get_cipher_name(ssl) << sep
       << "protocol=" << SSL_get_version(ssl) << sep;
}

} // namespace brpc

#endif // USE_MESALINK
