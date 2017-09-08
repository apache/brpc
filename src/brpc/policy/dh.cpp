// [ Modified from code in SRS2 (src/protocol/srs_rtmp_handshake.cpp:140) ]
// The MIT License (MIT)
// Copyright (c) 2013-2015 SRS(ossrs)
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "butil/logging.h"
#include "butil/ssl_compat.h"
#include "brpc/log.h"
#include "brpc/policy/dh.h"

namespace brpc {
namespace policy {

void DHWrapper::clear() {
    if (_pdh != NULL) {
        DH_free(_pdh);
        _pdh = NULL;
    }
}
    
int DHWrapper::initialize(bool ensure_128bytes_public_key) {
    for (;;) {
        if (do_initialize() != 0) {
            return -1;
        }
        if (ensure_128bytes_public_key) {
            const BIGNUM* pub_key = NULL;
            DH_get0_key(_pdh, &pub_key, NULL);
            int key_size = BN_num_bytes(pub_key);
            if (key_size != 128) {
                RPC_VLOG << "regenerate 128B key, current=" << key_size;
                clear();
                continue;
            }
        }
        break;
    }
    return 0;
}
    
int DHWrapper::copy_public_key(char* pkey, int* pkey_size) const {
    const BIGNUM* pub_key = NULL;
    DH_get0_key(_pdh, &pub_key, NULL);
    // copy public key to bytes.
    // sometimes, the key_size is 127, seems ok.
    int key_size = BN_num_bytes(pub_key);
    CHECK_GT(key_size, 0);
        
    // maybe the key_size is 127, but dh will write all 128bytes pkey,
    // no need to set/initialize the pkey.
    // @see https://github.com/ossrs/srs/issues/165
    key_size = BN_bn2bin(pub_key, (unsigned char*)pkey);
    CHECK_GT(key_size, 0);
        
    // output the size of public key.
    // @see https://github.com/ossrs/srs/issues/165
    CHECK_LE(key_size, *pkey_size);
    *pkey_size = key_size;
    return 0;
}
    
int DHWrapper::copy_shared_key(const void* ppkey, int ppkey_size,
                           void* skey, int* skey_size) const {
    BIGNUM* ppk = BN_bin2bn((const unsigned char*)ppkey, ppkey_size, 0);
    if (ppk == NULL) {
        LOG(ERROR) << "Fail to BN_bin2bn";
        return -1;
    }
    // @see https://github.com/ossrs/srs/issues/165
    int key_size = DH_compute_key((unsigned char*)skey, ppk, _pdh);
    if (key_size < 0 || key_size > *skey_size) {
        LOG(ERROR) << "Fail to compute shared key";
        BN_free(ppk);
        return -1;
    }
    *skey_size = key_size;
    return 0;
}
    
int DHWrapper::do_initialize() {
    BIGNUM* p = get_rfc2409_prime_1024(NULL);
    if (!p) {
        return -1;
    }
    // See RFC 2409, Section 6 "Oakley Groups"
    // for the reason why 2 is used as generator.
    BIGNUM* g = NULL;
    BN_dec2bn(&g, "2");
    if (!g) {
        BN_free(p);
        return -1;
    }
    _pdh = DH_new();
    if (!_pdh) {
        BN_free(p);
        BN_free(g);
        return -1;
    }
    DH_set0_pqg(_pdh, p, NULL, g);
    
    // Generate private and public key
    if (!DH_generate_key(_pdh)) {
        LOG(ERROR) << "Fail to DH_generate_key";
        return -1;
    }
    return 0;
}

}  // namespace policy
} // namespace brpc
