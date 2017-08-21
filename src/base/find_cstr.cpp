// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jun 23 15:03:24 CST 2015

#include "base/find_cstr.h"

namespace base {

__thread StringMapThreadLocalTemp tls_stringmap_temp = { false, {} };

}  // namespace base
