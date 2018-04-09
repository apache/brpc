// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Aug  3 12:46:15 CST 2014

#include <deque>
#include "butil/logging.h"
#include "bthread/butex.h"                       // butex_*
#include "bthread/mutex.h"
#include "bthread/list_of_abafree_id.h"
#include "butil/resource_pool.h"
#include "bthread/bthread.h"

namespace bthread {

// This queue reduces the chance to allocate memory for deque
template <typename T, int N>
class SmallQueue {
public:
    SmallQueue() : _begin(0), _size(0), _full(NULL) {}
    
    void push(const T& val) {
        if (_full != NULL && !_full->empty()) {
            _full->push_back(val);
        } else if (_size < N) {
            int tail = _begin + _size;
            if (tail >= N) {
                tail -= N;
            }
            _c[tail] = val;
            ++_size;
        } else {
            if (_full == NULL) {
                _full = new std::deque<T>;
            }
            _full->push_back(val);
        }
    }
    bool pop(T* val) {
        if (_size > 0) {
            *val = _c[_begin];
            ++_begin;
            if (_begin >= N) {
                _begin -= N;
            }
            --_size;
            return true;
        } else if (_full && !_full->empty()) {
            *val = _full->front();
            _full->pop_front();
            return true;
        }
        return false;
    }
    bool empty() const {
        return _size == 0 && (_full == NULL || _full->empty());
    }

    size_t size() const {
        return _size + (_full ? _full->size() : 0);
    }

    void clear() {
        _size = 0;
        _begin = 0;
        if (_full) {
            _full->clear();
        }
    }

    ~SmallQueue() {
        delete _full;
        _full = NULL;
    }
    
private:
    DISALLOW_COPY_AND_ASSIGN(SmallQueue);
    
    int _begin;
    int _size;
    T _c[N];
    std::deque<T>* _full;
};

struct PendingError {
    bthread_id_t id;
    int error_code;
    std::string error_text;
    const char *location;

    PendingError() : id(INVALID_BTHREAD_ID), error_code(0), location(NULL) {}
};

struct BAIDU_CACHELINE_ALIGNMENT Id {
    // first_ver ~ locked_ver - 1: unlocked versions
    // locked_ver: locked
    // unlockable_ver: locked and about to be destroyed
    // contended_ver: locked and contended
    uint32_t first_ver;
    uint32_t locked_ver;
    internal::FastPthreadMutex mutex;
    void* data;
    int (*on_error)(bthread_id_t, void*, int);
    int (*on_error2)(bthread_id_t, void*, int, const std::string&);
    const char *lock_location;
    uint32_t* butex;
    uint32_t* join_butex;
    SmallQueue<PendingError, 2> pending_q;
    
    Id() {
        // Although value of the butex(as version part of bthread_id_t)
        // does not matter, we set it to 0 to make program more deterministic.
        butex = bthread::butex_create_checked<uint32_t>();
        join_butex = bthread::butex_create_checked<uint32_t>();
        *butex = 0;
        *join_butex = 0;
    }

    ~Id() {
        bthread::butex_destroy(butex);
        bthread::butex_destroy(join_butex);
    }

    inline bool has_version(uint32_t id_ver) const {
        return id_ver >= first_ver && id_ver < locked_ver;
    }
    inline uint32_t contended_ver() const { return locked_ver + 1; }
    inline uint32_t unlockable_ver() const { return locked_ver + 2; }
    inline uint32_t last_ver() const { return unlockable_ver(); }
    
    // also the next "first_ver"
    inline uint32_t end_ver() const { return last_ver() + 1; }
};

BAIDU_CASSERT(sizeof(Id) % 64 == 0, sizeof_Id_must_align);

typedef butil::ResourceId<Id> IdResourceId;

inline bthread_id_t make_id(uint32_t version, IdResourceId slot) {
    const bthread_id_t tmp =
        { (((uint64_t)slot.value) << 32) | (uint64_t)version };
    return tmp;
}

inline IdResourceId get_slot(bthread_id_t id) {
    const IdResourceId tmp = { (id.value >> 32) };
    return tmp;
}

inline uint32_t get_version(bthread_id_t id) {
    return (uint32_t)(id.value & 0xFFFFFFFFul);
}

inline bool id_exists_with_true_negatives(bthread_id_t id) {
    Id* const meta = address_resource(get_slot(id));
    if (meta == NULL) {
        return false;
    }
    const uint32_t id_ver = bthread::get_version(id);
    return id_ver >= meta->first_ver && id_ver <= meta->last_ver();
}
// required by unittest
uint32_t id_value(bthread_id_t id) {
    Id* const meta = address_resource(get_slot(id));
    if (meta != NULL) {
        return *meta->butex;
    }
    return 0;  // valid version never be zero
}

static int default_bthread_id_on_error(bthread_id_t id, void*, int) {
    return bthread_id_unlock_and_destroy(id);
}
static int default_bthread_id_on_error2(
    bthread_id_t id, void*, int, const std::string&) {
    return bthread_id_unlock_and_destroy(id);
}

void id_status(bthread_id_t id, std::ostream &os) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        os << "Invalid id=" << id.value << '\n';
        return;
    }
    const uint32_t id_ver = bthread::get_version(id);
    uint32_t* butex = meta->butex;
    bool valid = true;
    void* data = NULL;
    int (*on_error)(bthread_id_t, void*, int) = NULL;
    int (*on_error2)(bthread_id_t, void*, int, const std::string&) = NULL;
    uint32_t first_ver = 0;
    uint32_t locked_ver = 0;
    uint32_t unlockable_ver = 0;
    uint32_t contended_ver = 0;
    const char *lock_location = NULL;
    SmallQueue<PendingError, 2> pending_q;
    uint32_t butex_value = 0;

    meta->mutex.lock();    
    if (meta->has_version(id_ver)) {
        data = meta->data;
        on_error = meta->on_error;
        on_error2 = meta->on_error2;
        first_ver = meta->first_ver;
        locked_ver = meta->locked_ver;
        unlockable_ver = meta->unlockable_ver();
        contended_ver = meta->contended_ver();
        lock_location = meta->lock_location;
        const size_t size = meta->pending_q.size();
        for (size_t i = 0; i < size; ++i) {
            PendingError front;
            meta->pending_q.pop(&front);
            meta->pending_q.push(front);
            pending_q.push(front);
        }
        butex_value = *butex;
    } else {
        valid = false;
    }
    meta->mutex.unlock();

    if (valid) {
        os << "First id: "
           << bthread::make_id(first_ver, bthread::get_slot(id)).value << '\n'
           << "Range: " << locked_ver - first_ver << '\n'
           << "Status: ";
        if (butex_value != first_ver) {
            os << "LOCKED at " << lock_location;
            if (butex_value == contended_ver) {
                os << " (CONTENDED)";
            } else if (butex_value == unlockable_ver) {
                os << " (ABOUT TO DESTROY)";
            } else {
                os << " (UNCONTENDED)";
            }
        } else {
            os << "UNLOCKED";
        }
        os << "\nPendingQ:";
        if (pending_q.empty()) {
            os << " EMPTY";
        } else {
            const size_t size = pending_q.size();
            for (size_t i = 0; i < size; ++i) {
                PendingError front;
                pending_q.pop(&front);
                os << " (" << front.location << "/E" << front.error_code
                   << '/' << front.error_text << ')';
            }
        }
        if (on_error) {
            if (on_error == default_bthread_id_on_error) {
                os << "\nOnError: unlock_and_destroy";
            } else {
                os << "\nOnError: " << (void*)on_error;
            }
        } else {
            if (on_error2 == default_bthread_id_on_error2) {
                os << "\nOnError2: unlock_and_destroy";
            } else {
                os << "\nOnError2: " << (void*)on_error2;
            }
        }
        os << "\nData: " << data;
    } else {
        os << "Invalid id=" << id.value;
    }
    os << '\n';
}

void id_pool_status(std::ostream &os) {
    os << butil::describe_resources<Id>() << '\n';
}

struct IdTraits {
    static const size_t BLOCK_SIZE = 63;
    static const size_t MAX_ENTRIES = 100000;
    static const bthread_id_t ID_INIT;
    static bool exists(bthread_id_t id)
    { return bthread::id_exists_with_true_negatives(id); }
};
const bthread_id_t IdTraits::ID_INIT = INVALID_BTHREAD_ID;

typedef ListOfABAFreeId<bthread_id_t, IdTraits> IdList;

struct IdResetter {
    explicit IdResetter(int ec, const std::string& et)
        : _error_code(ec), _error_text(et) {}
    void operator()(bthread_id_t & id) const {
        bthread_id_error2_verbose(
            id, _error_code, _error_text, __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__));
        id = INVALID_BTHREAD_ID;
    }
private:
    int _error_code;
    const std::string& _error_text;
};

size_t get_sizes(const bthread_id_list_t* list, size_t* cnt, size_t n) {
    if (list->impl == NULL) {
        return 0;
    }
    return static_cast<bthread::IdList*>(list->impl)->get_sizes(cnt, n);
}

const int ID_MAX_RANGE = 1024;

static int id_create_impl(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t, void*, int),
    int (*on_error2)(bthread_id_t, void*, int, const std::string&)) {
    IdResourceId slot;
    Id* const meta = get_resource(&slot);
    if (meta) {
        meta->data = data;
        meta->on_error = on_error;
        meta->on_error2 = on_error2;
        CHECK(meta->pending_q.empty());
        uint32_t* butex = meta->butex;
        if (0 == *butex || *butex + ID_MAX_RANGE + 2 < *butex) {
            // Skip 0 so that bthread_id_t is never 0
            // avoid overflow to make comparisons simpler.
            *butex = 1;
        }
        *meta->join_butex = *butex;
        meta->first_ver = *butex;
        meta->locked_ver = *butex + 1;
        *id = make_id(*butex, slot);
        return 0;
    }
    return ENOMEM;
}

static int id_create_ranged_impl(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t, void*, int),
    int (*on_error2)(bthread_id_t, void*, int, const std::string&),
    int range) {
    if (range < 1 || range > ID_MAX_RANGE) {
        LOG_IF(FATAL, range < 1) << "range must be positive, actually " << range;
        LOG_IF(FATAL, range > ID_MAX_RANGE ) << "max of range is " 
                << ID_MAX_RANGE << ", actually " << range;
        return EINVAL;
    }
    IdResourceId slot;
    Id* const meta = get_resource(&slot);
    if (meta) {
        meta->data = data;
        meta->on_error = on_error;
        meta->on_error2 = on_error2;
        CHECK(meta->pending_q.empty());
        uint32_t* butex = meta->butex;
        if (0 == *butex || *butex + ID_MAX_RANGE + 2 < *butex) {
            // Skip 0 so that bthread_id_t is never 0
            // avoid overflow to make comparisons simpler.
            *butex = 1;
        }
        *meta->join_butex = *butex;
        meta->first_ver = *butex;
        meta->locked_ver = *butex + range;
        *id = make_id(*butex, slot);
        return 0;
    }
    return ENOMEM;
}

}  // namespace bthread

extern "C" {

int bthread_id_create(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t, void*, int)) {
    return bthread::id_create_impl(
        id, data,
        (on_error ? on_error : bthread::default_bthread_id_on_error), NULL);
}

int bthread_id_create_ranged(bthread_id_t* id, void* data,
                             int (*on_error)(bthread_id_t, void*, int),
                             int range) {
    return bthread::id_create_ranged_impl(
        id, data, 
        (on_error ? on_error : bthread::default_bthread_id_on_error),
        NULL, range);
}

int bthread_id_lock_and_reset_range_verbose(
    bthread_id_t id, void **pdata, int range, const char *location) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    const uint32_t id_ver = bthread::get_version(id);
    uint32_t* butex = meta->butex;
    bool ever_contended = false;
    meta->mutex.lock();
    while (meta->has_version(id_ver)) {
        if (*butex == meta->first_ver) {
            // contended locker always wakes up the butex at unlock.
            meta->lock_location = location;
            if (range == 0) {
                // fast path
            } else if (range < 0 ||
                       range > bthread::ID_MAX_RANGE ||
                       range + meta->first_ver <= meta->locked_ver) {
                LOG_IF(FATAL, range < 0) << "range must be positive, actually "
                                         << range;
                LOG_IF(FATAL, range > bthread::ID_MAX_RANGE)
                    << "max range is " << bthread::ID_MAX_RANGE
                    << ", actually " << range;
            } else {
                meta->locked_ver = meta->first_ver + range;
            }
            *butex = (ever_contended ? meta->contended_ver() : meta->locked_ver);
            meta->mutex.unlock();
            if (pdata) {
                *pdata = meta->data;
            }
            return 0;
        } else if (*butex != meta->unlockable_ver()) {
            *butex = meta->contended_ver();
            uint32_t expected_ver = *butex;
            meta->mutex.unlock();
            ever_contended = true;
            if (bthread::butex_wait(butex, expected_ver, NULL) < 0 &&
                errno != EWOULDBLOCK && errno != EINTR) {
                return errno;
            }
            meta->mutex.lock();
        } else { // bthread_id_about_to_destroy was called.
            meta->mutex.unlock();
            return EPERM;
        }
    }
    meta->mutex.unlock();
    return EINVAL;
}

int bthread_id_error_verbose(bthread_id_t id, int error_code, 
                             const char *location) {
    return bthread_id_error2_verbose(id, error_code, std::string(), location);
}

int bthread_id_about_to_destroy(bthread_id_t id) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    const uint32_t id_ver = bthread::get_version(id);
    uint32_t* butex = meta->butex;
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        return EINVAL;
    }
    if (*butex == meta->first_ver) {
        meta->mutex.unlock();
        LOG(FATAL) << "bthread_id=" << id.value << " is not locked!";
        return EPERM;
    }
    const bool contended = (*butex == meta->contended_ver());
    *butex = meta->unlockable_ver();
    meta->mutex.unlock();
    if (contended) {
        // wake up all waiting lockers.
        bthread::butex_wake_except(butex, 0);
    }
    return 0;
}

int bthread_id_cancel(bthread_id_t id) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    uint32_t* butex = meta->butex;
    const uint32_t id_ver = bthread::get_version(id);
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        return EINVAL;
    }
    if (*butex != meta->first_ver) {
        meta->mutex.unlock();
        return EPERM;
    }       
    *butex = meta->end_ver();
    meta->first_ver = *butex;
    meta->locked_ver = *butex;
    meta->mutex.unlock();
    return_resource(bthread::get_slot(id));
    return 0;
}

int bthread_id_join(bthread_id_t id) {
    const bthread::IdResourceId slot = bthread::get_slot(id);
    bthread::Id* const meta = address_resource(slot);
    if (!meta) {
        // The id is not created yet, this join is definitely wrong.
        return EINVAL;
    }
    const uint32_t id_ver = bthread::get_version(id);
    uint32_t* join_butex = meta->join_butex;
    while (1) {
        meta->mutex.lock();
        const bool has_ver = meta->has_version(id_ver);
        const uint32_t expected_ver = *join_butex;
        meta->mutex.unlock();
        if (!has_ver) {
            break;
        }
        if (bthread::butex_wait(join_butex, expected_ver, NULL) < 0 &&
            errno != EWOULDBLOCK && errno != EINTR) {
            return errno;
        }
    }
    return 0;
}

int bthread_id_trylock(bthread_id_t id, void** pdata) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    uint32_t* butex = meta->butex;
    const uint32_t id_ver = bthread::get_version(id);
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        return EINVAL;
    }
    if (*butex != meta->first_ver) {
        meta->mutex.unlock();
        return EBUSY;
    }
    *butex = meta->locked_ver;
    meta->mutex.unlock();
    if (pdata != NULL) {
        *pdata = meta->data;
    }
    return 0;
}

int bthread_id_lock_verbose(bthread_id_t id, void** pdata,
                            const char *location) {
    return bthread_id_lock_and_reset_range_verbose(id, pdata, 0, location);
}

int bthread_id_unlock(bthread_id_t id) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    uint32_t* butex = meta->butex;
    // Release fence makes sure all changes made before signal visible to
    // woken-up waiters.
    const uint32_t id_ver = bthread::get_version(id);
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        LOG(FATAL) << "Invalid bthread_id=" << id.value;
        return EINVAL;
    }
    if (*butex == meta->first_ver) {
        meta->mutex.unlock();
        LOG(FATAL) << "bthread_id=" << id.value << " is not locked!";
        return EPERM;
    }
    bthread::PendingError front;
    if (meta->pending_q.pop(&front)) {
        meta->lock_location = front.location;
        meta->mutex.unlock();
        if (meta->on_error) {
            return meta->on_error(front.id, meta->data, front.error_code);
        } else {
            return meta->on_error2(front.id, meta->data, front.error_code,
                                   front.error_text);
        }
    } else {
        const bool contended = (*butex == meta->contended_ver());
        *butex = meta->first_ver;
        meta->mutex.unlock();
        if (contended) {
            // We may wake up already-reused id, but that's OK.
            bthread::butex_wake(butex);
        }
        return 0; 
    }
}

int bthread_id_unlock_and_destroy(bthread_id_t id) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    uint32_t* butex = meta->butex;
    uint32_t* join_butex = meta->join_butex;
    const uint32_t id_ver = bthread::get_version(id);
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        LOG(FATAL) << "Invalid bthread_id=" << id.value;
        return EINVAL;
    }
    if (*butex == meta->first_ver) {
        meta->mutex.unlock();
        LOG(FATAL) << "bthread_id=" << id.value << " is not locked!";
        return EPERM;
    }
    const uint32_t next_ver = meta->end_ver();
    *butex = next_ver;
    *join_butex = next_ver;
    meta->first_ver = next_ver;
    meta->locked_ver = next_ver;
    meta->pending_q.clear();
    meta->mutex.unlock();
    // Notice that butex_wake* returns # of woken-up, not successful or not.
    bthread::butex_wake_except(butex, 0);
    bthread::butex_wake_all(join_butex);
    return_resource(bthread::get_slot(id));
    return 0;
}

int bthread_id_list_init(bthread_id_list_t* list,
                         unsigned /*size*/,
                         unsigned /*conflict_size*/) {
    list->impl = NULL;  // create on demand.
    // Set unused fields to zero as well.
    list->head = 0;
    list->size = 0;
    list->conflict_head = 0;
    list->conflict_size = 0;
    return 0;
}

void bthread_id_list_destroy(bthread_id_list_t* list) {
    delete static_cast<bthread::IdList*>(list->impl);
    list->impl = NULL;
}

int bthread_id_list_add(bthread_id_list_t* list, bthread_id_t id) {
    if (list->impl == NULL) {
        list->impl = new (std::nothrow) bthread::IdList;
        if (NULL == list->impl) {
            return ENOMEM;
        }
    }
    return static_cast<bthread::IdList*>(list->impl)->add(id);
}

int bthread_id_list_reset(bthread_id_list_t* list, int error_code) {
    return bthread_id_list_reset2(list, error_code, std::string());
}

void bthread_id_list_swap(bthread_id_list_t* list1, 
                          bthread_id_list_t* list2) {
    std::swap(list1->impl, list2->impl);
}

int bthread_id_list_reset_pthreadsafe(bthread_id_list_t* list, int error_code,
                                       pthread_mutex_t* mutex) {
    return bthread_id_list_reset2_pthreadsafe(
        list, error_code, std::string(), mutex);
}

int bthread_id_list_reset_bthreadsafe(bthread_id_list_t* list, int error_code,
                                      bthread_mutex_t* mutex) {
    return bthread_id_list_reset2_bthreadsafe(
        list, error_code, std::string(), mutex);
}

}  // extern "C"

int bthread_id_create2(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t, void*, int, const std::string&)) {
    return bthread::id_create_impl(
        id, data, NULL,
        (on_error ? on_error : bthread::default_bthread_id_on_error2));
}

int bthread_id_create2_ranged(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t, void*, int, const std::string&),
    int range) {
    return bthread::id_create_ranged_impl(
        id, data, NULL,
        (on_error ? on_error : bthread::default_bthread_id_on_error2), range);
}

int bthread_id_error2_verbose(bthread_id_t id, int error_code,
                              const std::string& error_text,
                              const char *location) {
    bthread::Id* const meta = address_resource(bthread::get_slot(id));
    if (!meta) {
        return EINVAL;
    }
    const uint32_t id_ver = bthread::get_version(id);
    uint32_t* butex = meta->butex;
    meta->mutex.lock();
    if (!meta->has_version(id_ver)) {
        meta->mutex.unlock();
        return EINVAL;
    }
    if (*butex == meta->first_ver) {
        *butex = meta->locked_ver;
        meta->lock_location = location;
        meta->mutex.unlock();
        if (meta->on_error) {
            return meta->on_error(id, meta->data, error_code);
        } else {
            return meta->on_error2(id, meta->data, error_code, error_text);
        }
    } else {
        bthread::PendingError e;
        e.id = id;
        e.error_code = error_code;
        e.error_text = error_text;
        e.location = location;
        meta->pending_q.push(e);
        meta->mutex.unlock();
        return 0;
    }
}

int bthread_id_list_reset2(bthread_id_list_t* list,
                           int error_code,
                           const std::string& error_text) {
    if (list->impl != NULL) {
        static_cast<bthread::IdList*>(list->impl)->apply(
            bthread::IdResetter(error_code, error_text));
    }
    return 0;
}

int bthread_id_list_reset2_pthreadsafe(bthread_id_list_t* list,
                                       int error_code,
                                       const std::string& error_text,
                                       pthread_mutex_t* mutex) {
    if (mutex == NULL) {
        return EINVAL;
    }
    if (list->impl == NULL) {
        return 0;
    }
    bthread_id_list_t tmplist;
    const int rc = bthread_id_list_init(&tmplist, 0, 0);
    if (rc != 0) {
        return rc;
    }
    // Swap out the list then reset. The critical section is very small.
    pthread_mutex_lock(mutex);
    std::swap(list->impl, tmplist.impl);
    pthread_mutex_unlock(mutex);
    const int rc2 = bthread_id_list_reset2(&tmplist, error_code, error_text);
    bthread_id_list_destroy(&tmplist);
    return rc2;
}

int bthread_id_list_reset2_bthreadsafe(bthread_id_list_t* list,
                                       int error_code,
                                       const std::string& error_text,
                                       bthread_mutex_t* mutex) {
    if (mutex == NULL) {
        return EINVAL;
    }
    if (list->impl == NULL) {
        return 0;
    }
    bthread_id_list_t tmplist;
    const int rc = bthread_id_list_init(&tmplist, 0, 0);
    if (rc != 0) {
        return rc;
    }
    // Swap out the list then reset. The critical section is very small.
    bthread_mutex_lock(mutex);
    std::swap(list->impl, tmplist.impl);
    bthread_mutex_unlock(mutex);
    const int rc2 = bthread_id_list_reset2(&tmplist, error_code, error_text);
    bthread_id_list_destroy(&tmplist);
    return rc2;
}
