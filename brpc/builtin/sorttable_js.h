// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:57:02

#ifndef BRPC_BUILTIN_SORTTABLE_JS_H
#define BRPC_BUILTIN_SORTTABLE_JS_H

#include "base/iobuf.h"


namespace brpc {

// Get the sorttable.js as string or IOBuf.
// We need to pack all js inside C++ code so that builtin services can be
// accessed without external resources and network connection.
const char* sorttable_js();
const base::IOBuf& sorttable_js_iobuf();

} // namespace brpc


#endif // BRPC_BUILTIN_SORTTABLE_JS_H

