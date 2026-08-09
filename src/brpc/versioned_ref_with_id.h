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
#include "brpc/errno.pb.h"

namespace brpc {

// Unique identifier of a T object.
typedef uint64_t VRefId;

const VRefId INVALID_VREF_ID = (VRefId)-1;

template<typename T>
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


template <typename Ret>
typename std::enable_if<!butil::is_void<Ret>::value, Ret>::type ReturnEmpty() {
    return Ret{};
}

template <typename Ret>
typename std::enable_if<butil::is_void<Ret>::value, Ret>::type ReturnEmpty() {}

// Detect whether a type implements the member function `func_name' callable
// with Args..., exposing the result as a compile-time boolean:
//   HasMember_<func_name><V, Args...>::value
// The detector is decoupled from the caller so that it can also be reused in
// standalone static_assert to enforce interface contracts.
#define BRPC_DEFINE_MEMBER_DETECTOR(func_name)                                              \
    template <typename V, typename... Args>                                                 \
    struct HasMember##func_name {                                                          \
        template <typename U>                                                               \
        static auto Test(int) -> decltype(                                                  \
            std::declval<U>().func_name(std::declval<Args>()...), std::true_type());        \
        template <typename>                                                                 \
        static auto Test(...) -> std::false_type;                                           \
        static constexpr bool value = decltype(Test<V>(0))::value;                          \
    }

// Define a static caller `Call<func_name>' that invokes `obj->func_name(...)'
// if the type implements it, otherwise returns a default-constructed value.
// Requires the detector defined by BRPC_DEFINE_MEMBER_DETECTOR(func_name).
// On C++20, an inline `requires' expression is used directly (no separate
// detector needed);
// On C++17, a single `if constexpr' branch with the detector;
// on C++11/14, two SFINAE overloads so that the body referencing a
// possibly-missing member is never instantiated.
#if __cplusplus >= 202002L
#define BRPC_DEFINE_OPTIONAL_CALLER(func_name, return_type)                                 \
    template <typename U, typename... Args>                                                 \
    static return_type Call##func_name(U* obj, Args&&... args) {                             \
        if constexpr (requires { obj->func_name(std::forward<Args>(args)...); }) {          \
            BAIDU_CASSERT((butil::is_result_same<                                            \
                              return_type, decltype(&U::func_name), U, Args...>::value),     \
                          "Params or return type mismatch");                                 \
            return obj->func_name(std::forward<Args>(args)...);                              \
        } else {                                                                             \
            return ReturnEmpty<return_type>();                                               \
        }                                                                                    \
    }
#elif __cplusplus >= 201703L
#define BRPC_DEFINE_OPTIONAL_CALLER(func_name, return_type)                                 \
    template <typename U, typename... Args>                                                 \
    static return_type Call##func_name(U* obj, Args&&... args) {                           \
        if constexpr (HasMember##func_name<U, Args...>::value) {                           \
            BAIDU_CASSERT((butil::is_result_same<                                           \
                              return_type, decltype(&U::func_name), U, Args...>::value),    \
                          "Params or return type mismatch");                                \
            return obj->func_name(std::forward<Args>(args)...);                             \
        } else {                                                                            \
            return ReturnEmpty<return_type>();                                              \
        }                                                                                   \
    }
#else
#define BRPC_DEFINE_OPTIONAL_CALLER(func_name, return_type)                                 \
    template <typename U, typename... Args>                                                 \
    static typename std::enable_if<                                                         \
        HasMember##func_name<U, Args...>::value, return_type>::type                        \
    Call##func_name(U* obj, Args&&... args) {                                              \
        BAIDU_CASSERT((butil::is_result_same<                                               \
                          return_type, decltype(&U::func_name), U, Args...>::value),        \
                      "Params or return type mismatch");                                    \
        return obj->func_name(std::forward<Args>(args)...);                                 \
    }                                                                                       \
    template <typename U, typename... Args>                                                 \
    static typename std::enable_if<                                                         \
        !HasMember##func_name<U, Args...>::value, return_type>::type                       \
    Call##func_name(U*, Args&&...) {                                                        \
        return ReturnEmpty<return_type>();                                                  \
    }
#endif

// VersionedRefWithId is an efficient data structure, which can be find
// in O(1)-time by VRefId.
// Users shall call VersionedRefWithId::Create() to create T,
// store VRefId instead of T and call VersionedRefWithId::Address()
// to convert the identifier to an unique_ptr at each access. Whenever
// a unique_ptr is not destructed, the enclosed T will not be recycled.
//
// CRTP
// Derived classes implement 6 functions :
// 1. (required) int OnCreated(Args&&... args) :
//   Will be called in Create() to initialize T init when T is created successfully.
//   If initialization fails, return non-zero. VersionedRefWithId will be `SetFailed'
//   and Create() returns non-zero.
// 2. (required) void BeforeRecycled() :
//   Will be called in Dereference() before T is recycled.
// 3. (optional) void OnFailed(Args&&... args) :
//   Will be called in SetFailed() when VersionedRefWithId is set failed successfully.
// 4. (optional) void BeforeAdditionalRefReleased() :
//   Will be called in ReleaseAdditionalReference() before additional ref is released.
// 5. (optional) void AfterRevived() :
//   Will be called in Revive() When VersionedRefWithId is revived.
// 6. (optional) std::string OnDescription() const :
//   Will be called in description().
//
// Example usage:
//
// class UserData : public brpc::VersionedRefWithId<UserData> {
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
// friend class brpc::VersionedRefWithId<UserData>;
//
//     int OnCreated() {
//         _count.store(1, butil::memory_order_relaxed);
//         return 0;
//     }
//     void OnFailed(int error_code, const std::string& error_text) {
//         _count.fetch_sub(1, butil::memory_order_relaxed);
//     }
//     void BeforeRecycled() {
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
// if (UserData::Create(&id) ! =0) {
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
// UserData::SetFailedById(id);
//
template<typename T>
class VersionedRefWithId {
protected:
    struct Forbidden {};

public:
    explicit VersionedRefWithId(Forbidden)
        // Must be even because Address() relies on evenness of version.
        : _versioned_ref(0)
        , _this_id(0)
        , _additional_ref_status(ADDITIONAL_REF_USING) {}

    // Non-virtual on purpose: CRTP static polymorphism needs no vtable, and
    // instances are always recycled via return_resource() (never deleted
    // through a base pointer), so a virtual destructor would only add a
    // useless vptr and hurt cacheline layout.
    ~VersionedRefWithId() = default;
    DISALLOW_COPY_AND_ASSIGN(VersionedRefWithId);

    // Create a VersionedRefWithId, put the identifier into `id'.
    // `args' will be passed to OnCreated() directly.
    // Returns 0 on success, -1 otherwise.
    template<typename... Args>
    static int Create(VRefId* id, Args&&... args);

    // Place the VersionedRefWithId associated with identifier `id' into
    // unique_ptr `ptr', which will be released automatically when out
    // of scope (w/o explicit std::move). User can still access `ptr'
    // after calling ptr->SetFailed() before release of `ptr'.
    // This function is wait-free.
    // Returns 0 on success, -1 when the object was SetFailed().
    static int Address(VRefId id, VersionedRefWithIdUniquePtr<T>* ptr);

    // Returns 0 on success, 1 on failed object, -1 on recycled.
    static int AddressFailedAsWell(VRefId id, VersionedRefWithIdUniquePtr<T>* ptr);

    // Re-address current VersionedRefWithId into `ptr'.
    // Always succeed even if this object is failed.
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
    // fields of the object are still accessible after calling this
    // function. Calling SetFailed() of a VersionedRefWithId more than
    // once is OK.
    // T::OnFailed() will be called when SetFailed() successfully.
    // This function is lock-free.
    // Returns -1 when the object was already SetFailed(), 0 otherwise.
    template<typename... Args>
    static int SetFailedById(VRefId id, Args&&... args);

    template<typename... Args>
    int SetFailed(Args&&... args);

    bool Failed() const {
        return VersionOfVRef(_versioned_ref.load(butil::memory_order_relaxed))
            != VersionOfVRefId(_this_id);
    }

    // Release the additional reference which added inside `Create'
    // before so that `VersionedRefWithId' will be recycled automatically
    // once on one is addressing it.
    int ReleaseAdditionalReference();

    VRefId id() const { return _this_id; }

    // A brief description.
    std::string description() const;

protected:
friend void DereferenceVersionedRefWithId<>(T* r);

    // Status flag used to mark that
    enum AdditionalRefStatus {
        // 1. Additional reference has been increased;
        ADDITIONAL_REF_USING,
        // 2. Additional reference is increasing;
        ADDITIONAL_REF_REVIVING,
        // 3. Additional reference has been decreased.
        ADDITIONAL_REF_RECYCLED
    };

    AdditionalRefStatus additional_ref_status() const {
        return _additional_ref_status.load(butil::memory_order_relaxed);
    }

    uint64_t versioned_ref() const {
        // The acquire fence pairs with release fence in Dereference to avoid
        // inconsistent states to be seen by others.
        return _versioned_ref.load(butil::memory_order_acquire);
    }

    template<typename... Args>
    int SetFailedImpl(Args&&... args);

    // Release the reference. If no one is addressing this VersionedRefWithId,
    // it will be recycled automatically and T::BeforeRecycled() will be called.
    int Dereference();

    // Increase the reference count by 1.
    void AddReference() {
        _versioned_ref.fetch_add(1, butil::memory_order_release);
    }

    // Make this object addressable again.
    // If nref is less than `at_least_nref', VersionedRefWithId was
    // abandoned during revival and cannot be revived.
    void Revive(int32_t at_least_nref);

private:
    typedef butil::ResourceId<T> resource_id_t;

    // 1. When `failed_as_well=true', returns 0 on success,
    //    1 on failed object, -1 on recycled.
    // 2. When `failed_as_well=true', returns 0 on success,
    //    -1 when the object was SetFailed().
    static int AddressImpl(VRefId id, bool failed_as_well,
                           VersionedRefWithIdUniquePtr<T>* ptr);

    // Detectors + static callers for optional Derived-class callbacks.
    BRPC_DEFINE_MEMBER_DETECTOR(OnFailed);
    BRPC_DEFINE_OPTIONAL_CALLER(OnFailed, void);
    BRPC_DEFINE_MEMBER_DETECTOR(BeforeAdditionalRefReleased);
    BRPC_DEFINE_OPTIONAL_CALLER(BeforeAdditionalRefReleased, void);
    BRPC_DEFINE_MEMBER_DETECTOR(AfterRevived);
    BRPC_DEFINE_OPTIONAL_CALLER(AfterRevived, void);
    BRPC_DEFINE_MEMBER_DETECTOR(OnDescription);
    BRPC_DEFINE_OPTIONAL_CALLER(OnDescription, std::string);

    // unsigned 32-bit version + signed 32-bit referenced-count.
    // Meaning of version:
    // * Created version: no SetFailed() is called on the VersionedRefWithId yet.
    //   Must be same evenness with initial _versioned_ref because during lifetime
    //   of a VersionedRefWithId on the slot, the version is added with 1 twice.
    //   This is also the version encoded in VRefId.
    // * Failed version: = created version + 1, SetFailed()-ed but returned.
    // * Other versions: the object is already recycled.
    butil::atomic<uint64_t> BAIDU_CACHELINE_ALIGNMENT _versioned_ref;
    // The unique identifier.
    VRefId _this_id;
    // Indicates whether additional reference has increased,
    // decreased, or is increasing.
    // additional ref status:
    // constructor / `Create': REF_USING
    // `SetFailed': REF_USING -> REF_RECYCLED
    // `Revive' REF_RECYCLED -> REF_REVIVING -> REF_USING
    butil::atomic<AdditionalRefStatus> _additional_ref_status;
};

template<typename T>
void DereferenceVersionedRefWithId(T* r) {
    if (r) {
        static_cast<VersionedRefWithId<T>*>(r)->Dereference();
    }
}

template <typename T>
template<typename... Args>
int VersionedRefWithId<T>::Create(VRefId* id, Args&&... args) {
    resource_id_t slot;
    T* const t = butil::get_resource(&slot, Forbidden());
    if (t == NULL) {
        LOG(FATAL) << "Fail to get_resource<"
                   << butil::class_name<T>() << ">";
        return -1;
    }
    // nref can be non-zero due to concurrent Address().
    // _this_id will only be used in destructor/Destroy of referenced
    // slots, which is safe and properly fenced. Although it's better
    // to put the id into VersionedRefWithIdUniquePtr.
    VersionedRefWithId<T>* const vref_with_id = t;
    vref_with_id->_this_id = MakeVRefId<T>(
        VersionOfVRef(vref_with_id->_versioned_ref.fetch_add(
            1, butil::memory_order_release)), slot);
    vref_with_id->_additional_ref_status.store(
        ADDITIONAL_REF_USING, butil::memory_order_relaxed);
    BAIDU_CASSERT((butil::is_result_int<decltype(&T::OnCreated), T, Args...>::value),
                  "T::OnCreated must accept Args params and return int");
    // At last, call T::OnCreated() to initialize the T object.
    if (t->OnCreated(std::forward<Args>(args)...) != 0) {
        vref_with_id->SetFailed();
        // NOTE: This object may be recycled at this point,
        // don't touch anything.
        return -1;
    }
    *id = vref_with_id->_this_id;
    return 0;
}

template<typename T>
int VersionedRefWithId<T>::Address(
    VRefId id, VersionedRefWithIdUniquePtr<T>* ptr) {
    return AddressImpl(id, false, ptr);
}

template<typename T>
int VersionedRefWithId<T>::AddressFailedAsWell(
    VRefId id, VersionedRefWithIdUniquePtr<T>* ptr) {
    return AddressImpl(id, true, ptr);
}

template<typename T>
int VersionedRefWithId<T>::AddressImpl(
    VRefId id, bool failed_as_well, VersionedRefWithIdUniquePtr<T>* ptr) {
    const resource_id_t slot = SlotOfVRefId<T>(id);
    T* const t = address_resource(slot);
    if (__builtin_expect(t != NULL, 1)) {
        // acquire fence makes sure this thread sees latest changes before
        // Dereference() or Revive().
        VersionedRefWithId<T>* const vref_with_id = t;
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
                        BAIDU_CASSERT((butil::is_result_void<
                                          decltype(&T::BeforeRecycled), T>::value),
                                      "T::BeforeRecycled must accept Args params"
                                      " and return void");
                        t->BeforeRecycled();
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
            CHECK(false) << "Over dereferenced VRefId=" << id;
        }
    }
    return -1;
}

template<typename T>
void VersionedRefWithId<T>::ReAddress(VersionedRefWithIdUniquePtr<T>* ptr) {
    _versioned_ref.fetch_add(1, butil::memory_order_acquire);
    ptr->reset(static_cast<T*>(this));
}

template<typename T>
template<typename... Args>
int VersionedRefWithId<T>::SetFailedById(VRefId id, Args&&... args) {
    VersionedRefWithIdUniquePtr<T> ptr;
    if (Address(id, &ptr) != 0) {
        return -1;
    }
    return ptr->SetFailed(std::forward<Args>(args)...);
}

template<typename T>
template<typename... Args>
int VersionedRefWithId<T>::SetFailed(Args&&... args) {
    return SetFailedImpl(std::forward<Args>(args)...);
}

template<typename T>
template<typename... Args>
int VersionedRefWithId<T>::SetFailedImpl(Args&&... args) {
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
            CallOnFailed(static_cast<T*>(this), std::forward<Args>(args)...);
            // Deref additionally which is added at creation so that this
            // queue's reference will hit 0(recycle) when no one addresses it.
            ReleaseAdditionalReference();
            // NOTE: This object may be recycled at this point, don't
            // touch anything.
            return 0;
        }
    }
}

template<typename T>
int VersionedRefWithId<T>::ReleaseAdditionalReference() {
    do {
        AdditionalRefStatus expect = ADDITIONAL_REF_USING;
        if (_additional_ref_status.compare_exchange_strong(
                expect, ADDITIONAL_REF_RECYCLED,
                butil::memory_order_relaxed,
                butil::memory_order_relaxed)) {
            CallBeforeAdditionalRefReleased(static_cast<T*>(this));
            return Dereference();
        }

        if (expect == ADDITIONAL_REF_REVIVING) {
            // sched_yield to wait until status is not REF_REVIVING.
            sched_yield();
        } else {
            return -1; // REF_RECYCLED
        }
    } while (true);
}

template<typename T>
int VersionedRefWithId<T>::Dereference() {
    const VRefId id = _this_id;
    const uint64_t vref = _versioned_ref.fetch_sub(
        1, butil::memory_order_release);
    const int32_t nref = NRefOfVRef(vref);
    if (nref > 1) {
        return 0;
    }
    if (BAIDU_LIKELY(nref == 1)) {
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
        // had `SetFailed' this object before. We should destroy the
        // object under both situation
        if (BAIDU_LIKELY(ver == id_ver || ver == id_ver + 1)) {
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
                static_cast<T*>(this)->BeforeRecycled();
                return_resource(SlotOfVRefId<T>(id));
                return 1;
            }
            return 0;
        }
        LOG(FATAL) << "Invalid VRefId=" << id;
        return -1;
    }
    LOG(FATAL) << "Over dereferenced VRefId=" << id;
    return -1;
}

template<typename T>
void VersionedRefWithId<T>::Revive(int32_t at_least_nref) {
    const uint32_t id_ver = VersionOfVRefId(_this_id);
    uint64_t vref = _versioned_ref.load(butil::memory_order_relaxed);
    _additional_ref_status.store(
        ADDITIONAL_REF_REVIVING, butil::memory_order_relaxed);
    while (true) {
        CHECK_EQ(id_ver + 1, VersionOfVRef(vref)) << "id=" << id();

        int32_t nref = NRefOfVRef(vref);
        if (nref < at_least_nref) {
            // Set the status to REF_RECYCLED since no one uses this object
            _additional_ref_status.store(
                ADDITIONAL_REF_RECYCLED, butil::memory_order_relaxed);
            CHECK_EQ(1, nref);
            LOG(WARNING) << description() << " was abandoned during revival";
            return;
        }
        // +1 is the additional ref added in Create(). TODO(gejun): we should
        // remove this additional nref someday.
        if (_versioned_ref.compare_exchange_weak(
                vref, MakeVRef(id_ver, nref + 1/*note*/),
                butil::memory_order_release,
                butil::memory_order_relaxed)) {
            // Set the status to REF_USING since we add additional ref again
            _additional_ref_status.store(
                ADDITIONAL_REF_USING, butil::memory_order_relaxed);
            CallAfterRevived(static_cast<T*>(this));
            return;
        }
    }
}

template<typename T>
std::string VersionedRefWithId<T>::description() const {
    std::string result;
    result.reserve(128);
    butil::string_appendf(&result, "%s{id=%" PRIu64 " ", butil::class_name<T>(), id());
    result.append(CallOnDescription(const_cast<T*>(static_cast<const T*>(this))));
    butil::string_appendf(&result, "} (%p)", this);
    return result;
}

} // namespace brpc

#endif // BRPC_VERSIONED_REF_WITH_ID_H
