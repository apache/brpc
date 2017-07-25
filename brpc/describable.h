// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Dec 31 21:51:29 CST 2014

#ifndef BRPC_DESCRIBABLE_H
#define BRPC_DESCRIBABLE_H

#include <ostream>
#include "base/class_name.h"


namespace brpc {

struct DescribeOptions {
    DescribeOptions() : verbose(true), use_html(false) {}

    bool verbose;
    bool use_html;
};

class Describable {
public:
    virtual ~Describable() {}
    virtual void Describe(std::ostream& os, const DescribeOptions&) const {
        os << base::class_name_str(*this);
    }
};

class NonConstDescribable {
public:
    virtual ~NonConstDescribable() {}
    virtual void Describe(std::ostream& os, const DescribeOptions&) {
        os << base::class_name_str(*this);
    }
};

inline std::ostream& operator<<(std::ostream& os, const Describable& obj) {
    DescribeOptions options;
    options.verbose = false;
    obj.Describe(os, options);
    return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                NonConstDescribable& obj) {
    DescribeOptions options;
    options.verbose = false;
    obj.Describe(os, options);
    return os;
}

} // namespace brpc


#endif  // BRPC_DESCRIBABLE_H
