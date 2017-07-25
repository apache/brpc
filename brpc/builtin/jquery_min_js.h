// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:57:02

#ifndef BRPC_BUILTIN_JQUERY_MIN_JS_H
#define BRPC_BUILTIN_JQUERY_MIN_JS_H

#include "base/iobuf.h"


namespace brpc {

// Get the jquery.min.js as string or IOBuf.
// We need to pack all js inside C++ code so that builtin services can be
// accessed without external resources and network connection.
const char* jquery_min_js();
const base::IOBuf& jquery_min_js_iobuf();
const base::IOBuf& jquery_min_js_iobuf_gzip();

} // namespace brpc


#endif // BRPC_BUILTIN_JQUERY_MIN_JS_H

