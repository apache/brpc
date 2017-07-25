// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon May 11 15:22:39 CST 2015

#ifndef BRPC_SHARED_OBJECT_H
#define BRPC_SHARED_OBJECT_H

#include "base/intrusive_ptr.hpp"                   // base::intrusive_ptr
#include "base/atomicops.h"


namespace brpc {

// Inherit this class to be intrusively shared. Comparing to shared_ptr,
// intrusive_ptr saves one malloc (for shared_count) and gets better cache
// locality when the ref/deref are frequent, in the cost of inability of
// weak_ptr and worse interfacing.
class SharedObject {
friend void intrusive_ptr_add_ref(SharedObject* obj);
friend void intrusive_ptr_release(SharedObject* obj);

public:
    SharedObject() : _nref(0) { }
    int ref_count() const { return _nref.load(base::memory_order_relaxed); }
    
    // Add ref and returns the ref_count seen before added.
    // The effect is basically same as base::intrusive_ptr<T>(obj).detach()
    // except that the latter one does not return the seen ref_count which is
    // useful in some scenarios.
    int AddRefManually()
    { return _nref.fetch_add(1, base::memory_order_relaxed); }

    // Remove one ref, if the ref_count hit zero, delete this object.
    // Same as base::intrusive_ptr<T>(obj, false).reset(NULL)
    void RemoveRefManually() {
        if (_nref.fetch_sub(1, base::memory_order_release) == 1) {
            base::atomic_thread_fence(base::memory_order_acquire);
            delete this;
        }
    }

protected:
    virtual ~SharedObject() { }
private:
    base::atomic<int> _nref;
};

inline void intrusive_ptr_add_ref(SharedObject* obj) {
    obj->AddRefManually();
}

inline void intrusive_ptr_release(SharedObject* obj) {
    obj->RemoveRefManually();
}

} // namespace brpc


#endif // BRPC_SHARED_OBJECT_H

