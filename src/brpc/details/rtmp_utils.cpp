// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep 18 11:19:44 CST 2016

#include "brpc/details/rtmp_utils.h"


namespace brpc {

int avc_nalu_read_uev(BitStream* stream, int32_t* v) {
    if (stream->empty()) {
        return -1;
    }
    // ue(v) in 9.1 Parsing process for Exp-Golomb codes
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 227.
    // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
    //      leadingZeroBits = -1;
    //      for( b = 0; !b; leadingZeroBits++ )
    //          b = read_bits( 1 )
    // The variable codeNum is then assigned as follows:
    //      codeNum = (2<<leadingZeroBits) - 1 + read_bits( leadingZeroBits )
    int leadingZeroBits = -1;
    for (int8_t b = 0; !b && !stream->empty(); leadingZeroBits++) {
        b = stream->read_bit();
    }
    if (leadingZeroBits >= 31) {
        return -1;
    }
    int32_t result = (1 << leadingZeroBits) - 1;
    for (int i = 0; i < leadingZeroBits; i++) {
        int32_t b = stream->read_bit();
        result += b << (leadingZeroBits - 1);
    }
    *v = result;
    return 0;
}

int avc_nalu_read_bit(BitStream* stream, int8_t* v) {
    if (stream->empty()) {
        return -1;
    }
    *v = stream->read_bit();
    return 0;
}

} // namespace brpc

