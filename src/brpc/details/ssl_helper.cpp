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



#ifndef USE_MESALINK

#include <sys/socket.h>                // recv
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "butil/unique_ptr.h"
#include "butil/logging.h"
#include "butil/ssl_compat.h"
#include "butil/string_splitter.h"
#include "brpc/socket.h"
#include "brpc/details/ssl_helper.h"

namespace brpc {

#ifndef OPENSSL_NO_DH
static DH* g_dh_1024 = NULL;
static DH* g_dh_2048 = NULL;
static DH* g_dh_4096 = NULL;
static DH* g_dh_8192 = NULL;
#endif  // OPENSSL_NO_DH

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
        if (strncasecmp(protocol.data(), "SSLv3", protocol.size()) == 0) {
            protocol_flag |= SSLv3;
        } else if (strncasecmp(protocol.data(), "TLSv1", protocol.size()) == 0) {
            protocol_flag |= TLSv1;
        } else if (strncasecmp(protocol.data(), "TLSv1.1", protocol.size()) == 0) {
            protocol_flag |= TLSv1_1;
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

static void SSLInfoCallback(const SSL* ssl, int where, int ret) {
    (void)ret;
    SocketUniquePtr s;
    SocketId id = (SocketId)SSL_get_app_data((SSL*)ssl);
    if (Socket::Address(id, &s) != 0) {
        // Already failed
        return;
    }

    if (where & SSL_CB_HANDSHAKE_START) {
        if (s->ssl_state() == SSL_CONNECTED) {
            // Disable renegotiation (CVE-2009-3555)
            LOG(ERROR) << "Close " << *s << " due to insecure "
                       << "renegotiation detected (CVE-2009-3555)";
            s->SetFailed();
        }
    }
}

static void SSLMessageCallback(int write_p, int version, int content_type,
                               const void* buf, size_t len, SSL* ssl, void* arg) {
    (void)version;
    (void)arg;
#ifdef TLS1_RT_HEARTBEAT
    // Test heartbeat received (write_p is set to 0 for a received record)
    if ((content_type == TLS1_RT_HEARTBEAT) && (write_p == 0)) {
        const unsigned char* p = (const unsigned char*)buf;

        // Check if this is a CVE-2014-0160 exploitation attempt. 
        if (*p != TLS1_HB_REQUEST) {
            return;
        }

        // 1 type + 2 size + 0 payload + 16 padding
        if (len >= 1 + 2 + 16) {
            unsigned int payload = (p[1] * 256) + p[2];
            if (3 + payload + 16 <= len) {
                return;               // OK no problem
            }
        }
        
        // We have a clear heartbleed attack (CVE-2014-0160), the
        // advertised payload is larger than the advertised packet
        // length, so we have garbage in the buffer between the
        // payload and the end of the buffer (p+len). We can't know
        // if the SSL stack is patched, and we don't know if we can
        // safely wipe out the area between p+3+len and payload.
        // So instead, we prevent the response from being sent by
        // setting the max_send_fragment to 0 and we report an SSL
        // error, which will kill this connection. It will be reported
        // above as SSL_ERROR_SSL while an other handshake failure with
        // a heartbeat message will be reported as SSL_ERROR_SYSCALL.
        ssl->max_send_fragment = 0;
        SSLerr(SSL_F_TLS1_HEARTBEAT, SSL_R_SSL_HANDSHAKE_FAILURE);
        return;
    }
#endif // TLS1_RT_HEARTBEAT
}

#ifndef OPENSSL_NO_DH
static DH* SSLGetDHCallback(SSL* ssl, int exp, int keylen) {
    (void)exp;
    EVP_PKEY* pkey = SSL_get_privatekey(ssl);
    int type = pkey ? EVP_PKEY_base_id(pkey) : EVP_PKEY_NONE;

    // The keylen supplied by OpenSSL can only be 512 or 1024.
    // See ssl3_send_server_key_exchange() in ssl/s3_srvr.c
    if (type == EVP_PKEY_RSA || type == EVP_PKEY_DSA) {
        keylen = EVP_PKEY_bits(pkey);
    }

    if (keylen >= 8192) {
        return g_dh_8192;
    } else if (keylen >= 4096) {
        return g_dh_4096;
    } else if (keylen >= 2048) {
        return g_dh_2048;
    } else {
        return g_dh_1024;
    }
}
#endif  // OPENSSL_NO_DH

void ExtractHostnames(X509* x, std::vector<std::string>* hostnames) {
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    STACK_OF(GENERAL_NAME)* names = (STACK_OF(GENERAL_NAME)*)
            X509_get_ext_d2i(x, NID_subject_alt_name, NULL, NULL);
    if (names) {
        for (int i = 0; i < sk_GENERAL_NAME_num(names); i++) {
            char* str = NULL;
            GENERAL_NAME* name = sk_GENERAL_NAME_value(names, i);
            if (name->type == GEN_DNS) {
                if (ASN1_STRING_to_UTF8((unsigned char**)&str,
                                        name->d.dNSName) >= 0) {
                    std::string hostname(str);
                    hostnames->push_back(hostname);
                    OPENSSL_free(str);
                }
            }
        }
        sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
    }
#endif // SSL_CTRL_SET_TLSEXT_HOSTNAME 

    int i = -1;
    X509_NAME* xname = X509_get_subject_name(x);
    while ((i = X509_NAME_get_index_by_NID(xname, NID_commonName, i)) != -1) {
        char* str = NULL;
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(xname, i);
        const int len = ASN1_STRING_to_UTF8((unsigned char**)&str, 
                                            X509_NAME_ENTRY_get_data(entry));
        if (len >= 0) {
            std::string hostname(str, len);
            hostnames->push_back(hostname);
            OPENSSL_free(str);
        }
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
        PEM_read_bio_X509_AUX(cbio.get(), NULL, 0, NULL));
    if (!x) {
        LOG(ERROR) << "Fail to parse " << certificate << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }
    
    // Load the main certificate
    if (SSL_CTX_use_certificate(ctx, x.get()) != 1) {
        LOG(ERROR) << "Fail to load " << certificate << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }

    // Load the certificate chain
#if (OPENSSL_VERSION_NUMBER >= 0x10002000L)
    SSL_CTX_clear_chain_certs(ctx);
#else
    if (ctx->extra_certs != NULL) {
        sk_X509_pop_free(ctx->extra_certs, X509_free);
        ctx->extra_certs = NULL;
    }
#endif
    X509* ca = NULL;
    while ((ca = PEM_read_bio_X509(cbio.get(), NULL, 0, NULL))) {
        if (SSL_CTX_add_extra_chain_cert(ctx, ca) != 1) {
            LOG(ERROR) << "Fail to load chain certificate in "
                       << certificate << ": " << SSLError(ERR_get_error());
            X509_free(ca);
            return -1;
        }
    }

    int err = ERR_get_error();
    if (err != 0 && (ERR_GET_LIB(err) != ERR_LIB_PEM
                     || ERR_GET_REASON(err) != PEM_R_NO_START_LINE)) {
        LOG(ERROR) << "Fail to read chain certificate in "
                   << certificate << ": " << SSLError(err);
        return -1;
    }
    ERR_clear_error();

    // Validate certificate and private key 
    if (SSL_CTX_check_private_key(ctx) != 1) {
        LOG(ERROR) << "Fail to verify " << private_key << ": "
                   << SSLError(ERR_get_error());
        return -1;
    }

    if (hostnames != NULL) {
        ExtractHostnames(x.get(), hostnames);
    }
    return 0;
}

static int SetSSLOptions(SSL_CTX* ctx, const std::string& ciphers,
                         int protocols, const VerifyOptions& verify) {
    long ssloptions = SSL_OP_ALL    // All known workarounds for bugs
            | SSL_OP_NO_SSLv2
#ifdef SSL_OP_NO_COMPRESSION
            | SSL_OP_NO_COMPRESSION
#endif  // SSL_OP_NO_COMPRESSION
            | SSL_OP_CIPHER_SERVER_PREFERENCE
            | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;

    if (!(protocols & SSLv3)) {
        ssloptions |= SSL_OP_NO_SSLv3;
    }
    if (!(protocols & TLSv1)) {
        ssloptions |= SSL_OP_NO_TLSv1;
    }

#ifdef SSL_OP_NO_TLSv1_1
    if (!(protocols & TLSv1_1)) {
        ssloptions |= SSL_OP_NO_TLSv1_1;
    }
#endif  // SSL_OP_NO_TLSv1_1

#ifdef SSL_OP_NO_TLSv1_2
    if (!(protocols & TLSv1_2)) {
        ssloptions |= SSL_OP_NO_TLSv1_2;
    }
#endif  // SSL_OP_NO_TLSv1_2
    SSL_CTX_set_options(ctx, ssloptions);

    long sslmode = SSL_MODE_ENABLE_PARTIAL_WRITE
            | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER;
    SSL_CTX_set_mode(ctx, sslmode);

    if (!ciphers.empty() &&
        SSL_CTX_set_cipher_list(ctx, ciphers.c_str()) != 1) {
        LOG(ERROR) << "Fail to set cipher list to " << ciphers
                   << ": " << SSLError(ERR_get_error());
        return -1;
    }

    // TODO: Verify the CNAME in certificate matches the requesting host
    if (verify.verify_depth > 0) {
        SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER
                                 | SSL_VERIFY_FAIL_IF_NO_PEER_CERT), NULL);
        SSL_CTX_set_verify_depth(ctx, verify.verify_depth);
        std::string cafile = verify.ca_file_path;
        if (cafile.empty()) {
            cafile = X509_get_default_cert_area() + std::string("/cert.pem");
        }
        if (SSL_CTX_load_verify_locations(ctx, cafile.c_str(), NULL) == 0) {
            if (verify.ca_file_path.empty()) {
                LOG(WARNING) << "Fail to load default CA file " << cafile
                             << ": " << SSLError(ERR_get_error());
            } else {
                LOG(ERROR) << "Fail to load CA file " << cafile
                           << ": " << SSLError(ERR_get_error());
                return -1;
            }
        }
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    SSL_CTX_set_info_callback(ctx, SSLInfoCallback);
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
    // To detect and protect from heartbleed attack
    SSL_CTX_set_msg_callback(ctx, SSLMessageCallback);
#endif

    return 0;
}

static int ServerALPNCallback(
        SSL* ssl, const unsigned char** out, unsigned char* outlen,
        const unsigned char* in, unsigned int inlen, void* arg) {
    const std::string* alpns = static_cast<const std::string*>(arg);
    if (alpns == nullptr) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    // Use OpenSSL standard select API.
    int select_result = SSL_select_next_proto(
            const_cast<unsigned char**>(out), outlen, 
            reinterpret_cast<const unsigned char*>(alpns->data()), alpns->size(),
            in, inlen);
    return (select_result == OPENSSL_NPN_NEGOTIATED) 
                ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_NOACK;
}

static int SetServerALPNCallback(SSL_CTX* ssl_ctx, const std::string* alpns) {
    if (ssl_ctx == nullptr) {
        LOG(ERROR) << "Fail to set server ALPN callback, ssl_ctx is nullptr.";
        return -1;
    }

    // Server set alpn callback when openssl version is more than 1.0.2
#if (OPENSSL_VERSION_NUMBER >= SSL_VERSION_NUMBER(1, 0, 2))
    SSL_CTX_set_alpn_select_cb(ssl_ctx, ServerALPNCallback,
            const_cast<std::string*>(alpns));
#else
    LOG(WARNING) << "OpenSSL version=" << OPENSSL_VERSION_STR 
            << " is lower than 1.0.2, ignore server alpn.";
#endif
    return 0;
}

SSL_CTX* CreateClientSSLContext(const ChannelSSLOptions& options) {
    std::unique_ptr<SSL_CTX, FreeSSLCTX> ssl_ctx(
        SSL_CTX_new(SSLv23_client_method()));
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
                                const std::string* alpns,
                                std::vector<std::string>* hostnames) {
    std::unique_ptr<SSL_CTX, FreeSSLCTX> ssl_ctx(
        SSL_CTX_new(SSLv23_server_method()));
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

#ifdef SSL_MODE_RELEASE_BUFFERS
    if (options.release_buffer) {
        long sslmode = SSL_CTX_get_mode(ssl_ctx.get());
        sslmode |= SSL_MODE_RELEASE_BUFFERS;
        SSL_CTX_set_mode(ssl_ctx.get(), sslmode);
    }
#endif  // SSL_MODE_RELEASE_BUFFERS

    SSL_CTX_set_timeout(ssl_ctx.get(), options.session_lifetime_s);
    SSL_CTX_sess_set_cache_size(ssl_ctx.get(), options.session_cache_size);

#ifndef OPENSSL_NO_DH
    SSL_CTX_set_tmp_dh_callback(ssl_ctx.get(), SSLGetDHCallback);

#if !defined(OPENSSL_NO_ECDH) && defined(SSL_CTX_set_tmp_ecdh)
    EC_KEY* ecdh = NULL;
    int i = OBJ_sn2nid(options.ecdhe_curve_name.c_str());
    if (!i || ((ecdh = EC_KEY_new_by_curve_name(i)) == NULL)) {
        LOG(ERROR) << "Fail to find ECDHE named curve="
                   << options.ecdhe_curve_name
                   << ": " << SSLError(ERR_get_error());
        return NULL;
    }
    SSL_CTX_set_tmp_ecdh(ssl_ctx.get(), ecdh);
    EC_KEY_free(ecdh);
#endif

#endif  // OPENSSL_NO_DH

    // Set ALPN callback to choose application protocol when alpns is not empty.
    if (alpns != nullptr && !alpns->empty()) {
        if (SetServerALPNCallback(ssl_ctx.get(), alpns) != 0) {
            return NULL; 
        }
    }
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
    SSL_set_app_data(ssl, id);
    return ssl;
}

void AddBIOBuffer(SSL* ssl, int fd, int bufsize) {
    BIO* rbio = BIO_new(BIO_f_buffer());
    BIO_set_buffer_size(rbio, bufsize);
    BIO* rfd = BIO_new(BIO_s_fd());
    BIO_set_fd(rfd, fd, 0);
    rbio  = BIO_push(rbio, rfd);

    BIO* wbio = BIO_new(BIO_f_buffer());
    BIO_set_buffer_size(wbio, bufsize);
    BIO* wfd = BIO_new(BIO_s_fd());
    BIO_set_fd(wfd, fd, 0);
    wbio = BIO_push(wbio, wfd);
    SSL_set_bio(ssl, rbio, wbio);
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

#if OPENSSL_VERSION_NUMBER < 0x10100000L

// NOTE: Can't find a macro for CRYPTO_THREADID
//       Fallback to use CRYPTO_LOCK_ECDH as flag
#ifdef CRYPTO_LOCK_ECDH
static void SSLGetThreadId(CRYPTO_THREADID* tid) {
    CRYPTO_THREADID_set_numeric(tid, (unsigned long)pthread_self());
}
#else
static unsigned long SSLGetThreadId() {
    return pthread_self();
}
#endif  // CRYPTO_LOCK_ECDH

// Locks for SSL library
// NOTE: If we replace this with bthread_mutex_t, SSL routines
// may crash probably due to some TLS data used inside OpenSSL
// Also according to performance test, there is little difference
// between pthread mutex and bthread mutex
static butil::Mutex* g_ssl_mutexs = NULL;

static void SSLLockCallback(int mode, int n, const char* file, int line) {
    (void)file;
    (void)line;
    // Following log is too anonying even for verbose logs.
    // RPC_VLOG << "[" << file << ':' << line << "] SSL"
    //          << (mode & CRYPTO_LOCK ? "locks" : "unlocks")
    //          << " thread=" << CRYPTO_thread_id();
    if (mode & CRYPTO_LOCK) {
        g_ssl_mutexs[n].lock();
    } else {
        g_ssl_mutexs[n].unlock();
    }
}
#endif // OPENSSL_VERSION_NUMBER < 0x10100000L

int SSLThreadInit() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    g_ssl_mutexs = new butil::Mutex[CRYPTO_num_locks()];
    CRYPTO_set_locking_callback(SSLLockCallback);
# ifdef CRYPTO_LOCK_ECDH
    CRYPTO_THREADID_set_callback(SSLGetThreadId);
# else
    CRYPTO_set_id_callback(SSLGetThreadId);
# endif  // CRYPTO_LOCK_ECDH
#endif // OPENSSL_VERSION_NUMBER < 0x10100000L 
    return 0;
}

#ifndef OPENSSL_NO_DH

static DH* SSLGetDH1024() {
    BIGNUM* p = get_rfc2409_prime_1024(NULL);
    if (!p) {
        return NULL;
    }
    // See RFC 2409, Section 6 "Oakley Groups"
    // for the reason why 2 is used as generator.
    BIGNUM* g = NULL;
    BN_dec2bn(&g, "2");
    if (!g) {
        BN_free(p);
        return NULL;
    }
    DH *dh = DH_new();
    if (!dh) {
        BN_free(p);
        BN_free(g);
        return NULL;
    }
    DH_set0_pqg(dh, p, NULL, g);
    return dh;
}

static DH* SSLGetDH2048() {
    BIGNUM* p = get_rfc3526_prime_2048(NULL);
    if (!p) {
        return NULL;
    }
    // See RFC 3526, Section 3 "2048-bit MODP Group"
    // for the reason why 2 is used as generator.
    BIGNUM* g = NULL;
    BN_dec2bn(&g, "2");
    if (!g) {
        BN_free(p);
        return NULL;
    }
    DH* dh = DH_new();
    if (!dh) {
        BN_free(p);
        BN_free(g);
        return NULL;
    }
    DH_set0_pqg(dh, p, NULL, g);
    return dh;
}

static DH* SSLGetDH4096() {
    BIGNUM* p = get_rfc3526_prime_4096(NULL);
    if (!p) {
        return NULL;
    }
    // See RFC 3526, Section 5 "4096-bit MODP Group"
    // for the reason why 2 is used as generator.
    BIGNUM* g = NULL;
    BN_dec2bn(&g, "2");
    if (!g) {
        BN_free(p);
        return NULL;
    }
    DH *dh = DH_new();
    if (!dh) {
        BN_free(p);
        BN_free(g);
        return NULL;
    }
    DH_set0_pqg(dh, p, NULL, g);
    return dh;
}

static DH* SSLGetDH8192() {
    BIGNUM* p = get_rfc3526_prime_8192(NULL);
    if (!p) {
        return NULL;
    }
    // See RFC 3526, Section 7 "8192-bit MODP Group"
    // for the reason why 2 is used as generator.
    BIGNUM* g = NULL;
    BN_dec2bn(&g, "2");
    if (!g) {
        BN_free(g);
        return NULL;
    }
    DH *dh = DH_new();
    if (!dh) {
        BN_free(p);
        BN_free(g);
        return NULL;
    }
    DH_set0_pqg(dh, p, NULL, g);
    return dh;
}

#endif  // OPENSSL_NO_DH

int SSLDHInit() {
#ifndef OPENSSL_NO_DH
    if ((g_dh_1024 = SSLGetDH1024()) == NULL) {
        LOG(ERROR) << "Fail to initialize DH-1024";
        return -1;
    }
    if ((g_dh_2048 = SSLGetDH2048()) == NULL) {
        LOG(ERROR) << "Fail to initialize DH-2048";
        return -1;
    }
    if ((g_dh_4096 = SSLGetDH4096()) == NULL) {
        LOG(ERROR) << "Fail to initialize DH-4096";
        return -1;
    }
    if ((g_dh_8192 = SSLGetDH8192()) == NULL) {
        LOG(ERROR) << "Fail to initialize DH-8192";
        return -1;
    }
#endif  // OPENSSL_NO_DH
    return 0;
}

static std::string GetNextLevelSeparator(const char* sep) {
    if (sep[0] != '\n') {
        return sep;
    }
    const size_t left_len = strlen(sep + 1);
    if (left_len == 0) {
        return "\n ";
    }
    std::string new_sep;
    new_sep.reserve(left_len * 2 + 1);
    new_sep.append(sep, left_len + 1);
    new_sep.append(sep + 1, left_len);
    return new_sep;
}

void Print(std::ostream& os, SSL* ssl, const char* sep) {
    os << "cipher=" << SSL_get_cipher(ssl) << sep
       << "protocol=" << SSL_get_version(ssl) << sep
       << "verify=" << (SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER
                        ? "success" : "none");
    X509* cert = SSL_get_peer_certificate(ssl);
    if (cert) {
        os << sep << "peer_certificate={";
        const std::string new_sep = GetNextLevelSeparator(sep);
        if (sep[0] == '\n') {
            os << new_sep;
        }
        Print(os, cert, new_sep.c_str());
        if (sep[0] == '\n') {
            os << sep;
        }
        os << '}';
    }
}

void Print(std::ostream& os, X509* cert, const char* sep) {
    BIO* buf = BIO_new(BIO_s_mem());
    if (buf == NULL) {
        return;
    }
    BIO_printf(buf, "subject=");
    X509_NAME_print(buf, X509_get_subject_name(cert), 0);
    BIO_printf(buf, "%sstart_date=", sep);
    ASN1_TIME_print(buf, X509_get_notBefore(cert));
    BIO_printf(buf, "%sexpire_date=", sep);
    ASN1_TIME_print(buf, X509_get_notAfter(cert));

    BIO_printf(buf, "%scommon_name=", sep);
    std::vector<std::string> hostnames;
    brpc::ExtractHostnames(cert, &hostnames);
    for (size_t i = 0; i < hostnames.size(); ++i) {
        BIO_printf(buf, "%s;", hostnames[i].c_str());
    }

    BIO_printf(buf, "%sissuer=", sep);
    X509_NAME_print(buf, X509_get_issuer_name(cert), 0);

    char* bufp = NULL;
    int len = BIO_get_mem_data(buf, &bufp);
    os << butil::StringPiece(bufp, len);
}

std::string ALPNProtocolToString(const AdaptiveProtocolType& protocol) {
    butil::StringPiece name = protocol.name();
    // Default use http 1.1 version
    if (name.starts_with("http")) {
        name.set("http/1.1");
    }

    // ALPN extension uses 1 byte to record the protocol length
    // and it's maximum length is 255.
    if (name.size() > CHAR_MAX) {
        name = name.substr(0, CHAR_MAX); 
    }

    char length = static_cast<char>(name.size());
    return std::string(&length, 1) + name.data(); 
}

} // namespace brpc

#endif // USE_MESALINK
