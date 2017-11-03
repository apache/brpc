// Copyright (c) 2011 Baidu, Inc.
//
// RAII file descriptor.
//
// Example:
//    fd_guard fd1(open(...));
//    if (fd1 < 0) {
//        printf("Fail to open\n");
//        return -1;
//    }
//    if (another-error-happened) {
//        printf("Fail to do sth\n");
//        return -1;   // *** closing fd1 automatically ***
//    }
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BUTIL_FD_GUARD_H
#define BUTIL_FD_GUARD_H

#include "butil/files/scoped_file.h"

namespace butil {
namespace files {
class fd_guard : public ScopedFD {
public:
    operator int() const { return get(); }
};
}  // files
}  // base

#endif  // BUTIL_FD_GUARD_H
