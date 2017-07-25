// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Jun  8 18:07:38 CST 2016

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

#include "base/logging.h"
#include "brpc/policy/dh.h"


namespace brpc {
namespace policy {

#define RFC2409_PRIME_1024                              \
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"  \
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"  \
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"  \
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"  \
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381"  \
    "FFFFFFFFFFFFFFFF"

void DHWrapper::clear() {
    if (_pdh != NULL) {
        if (_pdh->p != NULL) {
            BN_free(_pdh->p);
            _pdh->p = NULL;
        }
        if (_pdh->g != NULL) {
            BN_free(_pdh->g);
            _pdh->g = NULL;
        }
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
            int key_size = BN_num_bytes(_pdh->pub_key);
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
    // copy public key to bytes.
    // sometimes, the key_size is 127, seems ok.
    int key_size = BN_num_bytes(_pdh->pub_key);
    CHECK_GT(key_size, 0);
        
    // maybe the key_size is 127, but dh will write all 128bytes pkey,
    // no need to set/initialize the pkey.
    // @see https://github.com/ossrs/srs/issues/165
    key_size = BN_bn2bin(_pdh->pub_key, (unsigned char*)pkey);
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
    int bits_count = 1024; 
        
    //1. Create the DH
    if ((_pdh = DH_new()) == NULL) {
        LOG(ERROR) << "Fail to DH_new";
        return -1;
    }
    
    //2. Create his internal p and g
    if ((_pdh->p = BN_new()) == NULL) {
        LOG(ERROR) << "Fail to BN_new _pdh->p";
        return -1;
    }
    if ((_pdh->g = BN_new()) == NULL) {
        LOG(ERROR) << "Fail to BN_new _pdh->g";
        return -1;
    }
    
    //3. initialize p and g, @see ./test/ectest.c:260
    if (!BN_hex2bn(&_pdh->p, RFC2409_PRIME_1024)) {
        LOG(ERROR) << "Fail to BN_hex2bn _pdh->p";
        return -1;
    }
    // @see ./test/bntest.c:1764
    if (!BN_set_word(_pdh->g, 2)) {
        LOG(ERROR) << "Fail to BN_set_word _pdh->g";
        return -1;
    }
    
    // 4. Set the key length
    _pdh->length = bits_count;
    
    // 5. Generate private and public key
    // @see ./test/dhtest.c:152
    if (!DH_generate_key(_pdh)) {
        LOG(ERROR) << "Fail to DH_generate_key";
        return -1;
    }
    return 0;
}

#undef RFC2409_PRIME_1024

}  // namespace policy
} // namespace brpc

