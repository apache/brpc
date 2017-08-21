// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Nov 18 14:00:06 CST 2016

#ifndef BRPC_DESTROYABLE_H
#define BRPC_DESTROYABLE_H

#include "base/unique_ptr.h"           // std::unique_ptr


namespace brpc {

class Destroyable {
public:
    virtual ~Destroyable() {}
    virtual void Destroy() = 0;
};

namespace detail {
template <typename T> struct Destroyer {
    void operator()(T* obj) const { if (obj) { obj->Destroy(); } }
};
}

// A special unique_ptr that calls "obj->Destroy()" instead of "delete obj".
template <typename T>
struct DestroyingPtr : public std::unique_ptr<T, detail::Destroyer<T> > {
    DestroyingPtr() {}
    DestroyingPtr(T* p) : std::unique_ptr<T, detail::Destroyer<T> >(p) {}
};

} // namespace brpc


#endif  // BRPC_DESTROYABLE_H
