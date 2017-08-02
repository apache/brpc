// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Oct 14 15:16:29 CST 2014

#ifndef BRPC_EXTENSION_H
#define BRPC_EXTENSION_H

#include <string>
#include <error.h>
#include "base/scoped_lock.h"
#include "base/logging.h"
#include "base/containers/case_ignored_flat_map.h"
#include "base/memory/singleton_on_pthread_once.h"

namespace base {
template <typename T> class GetLeakySingleton;
}


namespace brpc {

// A global map from string to user-extended instances (typed T).
// It's used by NamingService and LoadBalancer to maintain globally
// available instances.
// All names are case-insensitive. Names are printed in lowercases.

template <typename T>
class Extension {
public:
    static Extension<T>* instance();

    int Register(const std::string& name, T* instance);
    int RegisterOrDie(const std::string& name, T* instance);
    T* Find(const char* name);
    void List(std::ostream& os, char separator);

private:
friend class base::GetLeakySingleton<Extension<T> >;
    Extension();
    ~Extension();
    base::CaseIgnoredFlatMap<T*> _instance_map;
    base::Mutex _map_mutex;
};

} // namespace brpc


#include "brpc/extension_inl.h"

#endif  // BRPC_EXTENSION_H
