// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_VERSIONED_REF_WITH_ID_H
#define BRPC_VERSIONED_REF_WITH_ID_H

#include <memory>
#include "butil/resource_pool.h"
#include "butil/class_name.h"
#include "butil/logging.h"
#include "bthread/bthread.h"

namespace brpc {

// Unique identifier of a T object.
typedef uint64_t VRefId;

const VRefId INVALID_VREF_ID = (VRefId)-1;

template<typename T, typename CreateOptions>
class VersionedRefWithId;

template<typename T>
void DereferenceVersionedRefWithId(T* r);

template<typename T>
struct VersionedRefWithIdDeleter {
    void operator()(T* r) const {
        DereferenceVersionedRefWithId(r);
    }
};

template<typename T>
using VersionedRefWithIdUniquePtr =
    std::unique_ptr<T, VersionedRefWithIdDeleter<T>>;

// Utility functions to combine and extract VRefId.
template <typename T>
BUTIL_FORCE_INLINE VRefId MakeVRefId(uint32_t version,
                                     butil::ResourceId<T> slot) {
    return VRefId((((uint64_t)version) << 32) | slot.value);
}

template<typename T>
BUTIL_FORCE_INLINE butil::ResourceId<T> SlotOfVRefId(VRefId vref_id) {
    return { (vref_id & 0xFFFFFFFFul) };
}

BUTIL_FORCE_INLINE uint32_t VersionOfVRefId(VRefId vref_id) {
    return (uint32_t)(vref_id >> 32);
}

// Utility functions to combine and extract _versioned_ref
BUTIL_FORCE_INLINE uint32_t VersionOfVRef(uint64_t vref) {
    return (uint32_t)(vref >> 32);
}

BUTIL_FORCE_INLINE int32_t NRefOfVRef(uint64_t vref) {
    return (int32_t)(vref & 0xFFFFFFFFul);
}

BUTIL_FORCE_INLINE uint64_t MakeVRef(uint32_t version, int32_t nref) {
    // 1: Intended conversion to uint32_t, nref=-1 is 00000000FFFFFFFF
    return (((uint64_t)version) << 32) | (uint32_t/*1*/)nref;
}

// VersionedRefWithId is an efficient data structure, which can be find
// in O(1)-time by VRefId.
// Users shall call VersionedRefWithId::Create() to create T,
// store VRefId instead of T and call VersionedRefWithId::Address()
// to convert the identifier to an unique_ptr at each access. Whenever
// a unique_ptr is not destructed, the enclosed T will not be recycled.
//
// The implementation is the same as `Socket', but there is no health
// check and revive mechanism.
//
// Derived classes implement 4 virtual functions which are guaranteed
// to only be called once:
// 1. (required) int OnCreate(const CreateOptions* options) :
//   Will be called in Create() to initialize T init when T is created successfully.
//   If initialization fails, return non-zero. VersionedRefWithId will be `SetFailed'
//   and Create() returns non-zero.
// 2. (required) void OnRecycle() :
//   Will be called in Dereference() when T is being recycled.
// 3. (optional) void OnFailed() :
//   Will be called in SetFailed() when T is set failed successfully.
// 4. (optional) void OnAdditionalRefReleased() :
//   Will be called in ReleaseAdditionalReference().
//
// Example usage:
//
// class UserData : public brpc::VersionedRefWithId<UserData, void> {
// public:
//     explicit UserData(Forbidden f)
//         : brpc::VersionedRefWithId<UserData>(f)
//         , _count(0) {}

//     void Add(int c) {
//         _count.fetch_add(c, butil::memory_order_relaxed);
//     }
//     void Sub(int c) {
//         _count.fetch_sub(c, butil::memory_order_relaxed);
//     }
// private:
//     int OnCreate(const void*) override {
//         _count.store(1, butil::memory_order_relaxed);
//         return 0;
//     }
//     void OnFailed() override {
//         _count.fetch_sub(1, butil::memory_order_relaxed);
//     }
//     void OnRecycle() override {
//         _count.store(0, butil::memory_order_relaxed);
//     }
//
//     butil::atomic<int> _count;
// };
//
// typedef brpc::VRefId UserDataId;
// const brpc::VRefId INVALID_EVENT_DATA_ID = brpc::INVALID_VREF_ID;
// typedef brpc::VersionedRefWithIdUniquePtr<UserData> UserDataUniquePtr;
//
// And to call methods on UserData:
// UserDataId id;
// if (UserData::Create(NULL, &id) ! =0) {
//     LOG(ERROR) << "Fail to create UserData";
//     return;
// }
// UserDataUniquePtr user_data;
// if (UserData::Address(id, &user_data) != 0) {
//     LOG(ERROR) << "Fail to address UserDataId=" << id;
//     return;
// }
// user_data->Add(10);
// user_data->SetFailed();
// UserData::SetFailed(id);
//
template<typename T, typename CreateOptions>
class VersionedRefWithId {
protected:
    struct Forbidden {};

public:
    explicit VersionedRefWithId(Forbidden)
        : _versioned_ref(0)
        , _this_id(0)
        , _recycle_flag(false)
    {}

    virtual ~VersionedRefWithId() = default;
    DISALLOW_COPY_AND_ASSIGN(VersionedRefWithId);

    // Create a VersionedRefWithId, put the identifier into `id'.
    // `options' will be passed to OnCreate() directly.
    // Returns 0 on success, -1 otherwise.
    static int Create(const CreateOptions* options, VRefId* id);

    // Place the VersionedRefWithId associated with identifier `id' into
    // unique_ptr `ptr', which will be released automatically when out
    // of scope (w/o explicit std::move). User can still access `ptr'
    // after calling ptr->SetFailed() before release of `ptr'.
    // This function is wait-free.
    // Returns 0 on success, -1 when the Socket was SetFailed().
    static int Address(VRefId id, VersionedRefWithIdUniquePtr<T>* ptr);

    // Returns 0 on success, 1 on failed socket, -1 on recycled.
    static int AddressFailedAsWell(VRefId id, VersionedRefWithIdUniquePtr<T>* ptr);

    // Re-address current VersionedRefWithId into `ptr'.
    // Always succeed even if this socket is failed.
    void ReAddress(VersionedRefWithIdUniquePtr<T>* ptr);

    // Returns signed 32-bit referenced-count.
    int32_t nref() const {
        return NRefOfVRef(_versioned_ref.load(butil::memory_order_relaxed));
    }

    // Mark this VersionedRefWithId or the VersionedRefWithId associated
    // with `id' as failed.
    // Any later Address() of the identifier shall return NULL. The
    // VersionedRefWithId is NOT recycled after calling this function,
    // instead it will be recycled when no one references it. Internal
    // fields of the Socket are still accessible after calling this
    // function. Calling SetFailed() of a VersionedRefWithId more than
    // once is OK.
    // T::OnFailed() will be called when SetFailed() successfully.
    // This function is lock-free.
    // Returns -1 when the Socket was already SetFailed(), 0 otherwise.
    int SetFailed();
    static int SetFailed(VRefId id);

    bool Failed() const {
        return VersionOfVRef(_versioned_ref.load(butil::memory_order_relaxed))
            != VersionOfVRefId(_this_id);
    }

    // Release the additional reference which added inside `Create'
    // before so that `VersionedRefWithId' will be recycled automatically
    // once on one is addressing it.
    void ReleaseAdditionalReference();

    VRefId id() const { return _this_id; }

protected:
friend void DereferenceVersionedRefWithId<>(T* r);

    // 1. When `failed_ad_well=true', returns 0 on success,
    //    1 on failed socket, -1 on recycled.
    // 2. When `failed_ad_well=true', returns 0 on success,
    //    -1 when the Socket was SetFailed().
    static int AddressImpl(VRefId id, bool failed_as_well,
                           VersionedRefWithIdUniquePtr<T>* ptr);

    // Release the reference. If no one is addressing this VersionedRefWithId,
    // it will be recycled automatically and T::OnRecycle() will be called.
    int Dereference();

private:
    typedef butil::ResourceId<T> resource_id_t;

    // See comments of VersionedRefWithId above.
    virtual int OnCreate(const CreateOptions* options) = 0;
    virtual void OnRecycle() = 0;
    virtual void OnFailed() {}
    virtual void OnAdditionalRefReleased() {}

    // unsigned 32-bit version + signed 32-bit referenced-count.
    // Meaning of version:
    // * Created version: no SetFailed() is called on the VersionedRefWithId yet.
    //   Must be same evenness with initial _versioned_ref because during lifetime
    //   of a VersionedRefWithId on the slot, the version is added with 1 twice.
    //   This is also the version encoded in VRefId.
    // * Failed version: = created version + 1, SetFailed()-ed but returned.
    // * Other versions: the socket is already recycled.
    butil::atomic<uint64_t> BAIDU_CACHELINE_ALIGNMENT _versioned_ref;
    // The unique identifier.
    VRefId _this_id;
    // Flag used to mark whether additional reference
    // has been decreased by either `SetFailed'.
    butil::atomic<bool> _recycle_flag;
};


template<typename T>
void DereferenceVersionedRefWithId(T* r) {
    if (r) {
        r->Dereference();
    }
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::Create(const CreateOptions* options,
                                                 VRefId* id) {
    resource_id_t slot;
    T* const t = butil::get_resource(&slot, Forbidden());
    if (t == NULL) {
        LOG(FATAL) << "Fail to get_resource<"
                   << butil::class_name_str<T>() << ">";
        return -1;
    }
    // nref can be non-zero due to concurrent Address().
    // _this_id will only be used in destructor/Destroy of referenced
    // slots, which is safe and properly fenced. Although it's better
    // to put the id into VersionedRefWithIdUniquePtr.
    VersionedRefWithId<T, CreateOptions>* const vref_with_id = t;
    vref_with_id->_this_id = MakeVRefId<T>(
        VersionOfVRef(vref_with_id->_versioned_ref.fetch_add(
            1, butil::memory_order_release)), slot);
    vref_with_id->_recycle_flag.store(false, butil::memory_order_relaxed);
    // At last, call T::OnCreate() to initialize the T object.
    if (vref_with_id->OnCreate(options) != 0) {
        vref_with_id->SetFailed();
        // NOTE: This object may be recycled at this point, don't
        // touch anything.
        return -1;
    }
    *id = vref_with_id->_this_id;
    return 0;
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::Address(
    VRefId id, VersionedRefWithIdUniquePtr<T>* ptr) {
    return AddressImpl(id, false, ptr);
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::AddressFailedAsWell(
    VRefId id, VersionedRefWithIdUniquePtr<T>* ptr) {
    return AddressImpl(id, true, ptr);
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::AddressImpl(
    VRefId id, bool failed_as_well, VersionedRefWithIdUniquePtr<T>* ptr) {
    const resource_id_t slot = SlotOfVRefId<T>(id);
    T* const t = address_resource(slot);
    if (__builtin_expect(t != NULL, 1)) {
        // acquire fence makes sure this thread sees latest changes before
        // Dereference() or Revive().
        VersionedRefWithId<T, CreateOptions>* const vref_with_id = t;
        const uint64_t vref1 = vref_with_id->_versioned_ref.fetch_add(
            1, butil::memory_order_acquire);
        const uint32_t ver1 = VersionOfVRef(vref1);
        if (ver1 == VersionOfVRefId(id)) {
            ptr->reset(t);
            return 0;
        }
        if (failed_as_well && ver1 == VersionOfVRefId(id) + 1) {
            ptr->reset(t);
            return 1;
        }

        const uint64_t vref2 = vref_with_id->_versioned_ref.fetch_sub(
            1, butil::memory_order_release);
        const int32_t nref = NRefOfVRef(vref2);
        if (nref > 1) {
            return -1;
        } else if (__builtin_expect(nref == 1, 1)) {
            const uint32_t ver2 = VersionOfVRef(vref2);
            if ((ver2 & 1)) {
                if (ver1 == ver2 || ver1 + 1 == ver2) {
                    uint64_t expected_vref = vref2 - 1;
                    if (vref_with_id->_versioned_ref.compare_exchange_strong(
                            expected_vref, MakeVRef(ver2 + 1, 0),
                            butil::memory_order_acquire,
                            butil::memory_order_relaxed)) {
                        vref_with_id->OnRecycle();
                        return_resource(slot);
                    }
                } else {
                    CHECK(false) << "ref-version=" << ver1
                                 << " unref-version=" << ver2;
                }
            } else {
                // Addressed a free slot.
            }
        } else {
            CHECK(false) << "Over dereferenced SocketId=" << id;
        }
    }
    return -1;
}

template<typename T, typename CreateOptions>
void VersionedRefWithId<T, CreateOptions>::ReAddress(VersionedRefWithIdUniquePtr<T>* ptr) {
    _versioned_ref.fetch_add(1, butil::memory_order_acquire);
    ptr->reset(static_cast<T*>(this));
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::SetFailed() {
    const uint32_t id_ver = VersionOfVRefId(_this_id);
    uint64_t vref = _versioned_ref.load(butil::memory_order_relaxed);
    for (;;) {
        if (VersionOfVRef(vref) != id_ver) {
            return -1;
        }
        // Try to set version=id_ver+1 (to make later address() return NULL),
        // retry on fail.
        if (_versioned_ref.compare_exchange_strong(
            vref, MakeVRef(id_ver + 1, NRefOfVRef(vref)),
            butil::memory_order_release,
            butil::memory_order_relaxed)) {
            // Call T::OnFailed() to notify the failure of T.
            OnFailed();
            // Deref additionally which is added at creation so that this
            // queue's reference will hit 0(recycle) when no one addresses it.
            ReleaseAdditionalReference();
            // NOTE: This object may be recycled at this point, don't
            // touch anything.
            return 0;
        }
    }
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::SetFailed(VRefId id) {
    VersionedRefWithIdUniquePtr<T> ptr;
    if (Address(id, &ptr) != 0) {
        return -1;
    }
    return ptr->SetFailed();
}

template<typename T, typename CreateOptions>
void VersionedRefWithId<T, CreateOptions>::ReleaseAdditionalReference() {
    if (!_recycle_flag.exchange(true,butil::memory_order_relaxed)) {
        OnAdditionalRefReleased();
        Dereference();
    }
}

template<typename T, typename CreateOptions>
int VersionedRefWithId<T, CreateOptions>::Dereference() {
    const VRefId id = _this_id;
    const uint64_t vref = _versioned_ref.fetch_sub(
        1, butil::memory_order_release);
    const int32_t nref = NRefOfVRef(vref);
    if (nref > 1) {
        return 0;
    }
    if (__builtin_expect(nref == 1, 1)) {
        const uint32_t ver = VersionOfVRef(vref);
        const uint32_t id_ver = VersionOfVRefId(id);
        // Besides first successful SetFailed() adds 1 to version, one of
        // those dereferencing nref from 1->0 adds another 1 to version.
        // Notice "one of those": The wait-free Address() may make ref of a
        // version-unmatched slot change from 1 to 0 for mutiple times, we
        // have to use version as a guard variable to prevent returning the
        // VersionedRefWithId to pool more than once.
        //
        // Note: `ver == id_ver' means this VersionedRefWithId has been `SetRecycle'
        // before rather than `SetFailed'; `ver == ide_ver+1' means we
        // had `SetFailed' this socket before. We should destroy the
        // socket under both situation
        if (__builtin_expect(ver == id_ver || ver == id_ver + 1, 1)) {
            // sees nref:1->0, try to set version=id_ver+2,--nref.
            // No retry: if version changes, the slot is already returned by
            // another one who sees nref:1->0 concurrently; if nref changes,
            // which must be non-zero, the slot will be returned when
            // nref changes from 1->0 again.
            // Example:
            //   SetFailed(): --nref, sees nref:1->0           (1)
            //                try to set version=id_ver+2      (2)
            //    Address():  ++nref, unmatched version        (3)
            //                --nref, sees nref:1->0           (4)
            //                try to set version=id_ver+2      (5)
            // 1,2,3,4,5 or 1,3,4,2,5:
            //            SetFailed() succeeds, Address() fails at (5).
            // 1,3,2,4,5: SetFailed() fails with (2), the slot will be
            //            returned by (5) of Address()
            // 1,3,4,5,2: SetFailed() fails with (2), the slot is already
            //            returned by (5) of Address().
            uint64_t expected_vref = vref - 1;
            if (_versioned_ref.compare_exchange_strong(
                    expected_vref, MakeVRef(id_ver + 2, 0),
                    butil::memory_order_acquire,
                    butil::memory_order_relaxed)) {
                OnRecycle();
                return_resource(SlotOfVRefId<T>(id));
                return 1;
            }
            return 0;
        }
        LOG(FATAL) << "Invalid SocketId=" << id;
        return -1;
    }
    LOG(FATAL) << "Over dereferenced SocketId=" << id;
    return -1;
}

}

#endif // BRPC_VERSIONED_REF_WITH_ID_H
