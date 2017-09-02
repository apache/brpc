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

#ifndef BRPC_POLICY_DH_H
#define BRPC_POLICY_DH_H

#include <openssl/dh.h>


namespace brpc {
namespace policy {

// Diffie-Hellman key exchange
class DHWrapper {
public:
    DHWrapper() : _pdh(NULL) {}
    ~DHWrapper() { clear(); }
    
    // initialize dh, generate the public and private key.
    // set `ensure_128bytes_public_key' to true to ensure public key is 128bytes
    int initialize(bool ensure_128bytes_public_key = false);

    // copy the public key.
    // @param pkey the bytes to copy the public key.
    // @param pkey_size the max public key size, output the actual public key size.
    // user should never ignore this size.
    // @remark, when ensure_128bytes_public_key is true, the size always 128.
    int copy_public_key(char* pkey, int* pkey_size) const;

    // generate and copy the shared key.
    // generate the shared key with peer public key.
    // @param ppkey peer public key.
    // @param ppkey_size the size of ppkey.
    // @param skey the computed shared key.
    // @param skey_size the max shared key size, output the actual shared key size.
    // user should never ignore this size.
    int copy_shared_key(const void* ppkey, int ppkey_size,
                        void* skey, int* skey_size) const;
    
private:
    int do_initialize();
    void clear();
    
private:
    DH* _pdh;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_DH_H
