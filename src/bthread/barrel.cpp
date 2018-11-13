#include "bthread/barrel.h"
#include "butil/hash.h"
#include "butil/memory/singleton.h"
#include <gflags/gflags.h>
#include <assert.h>

DEFINE_int32(uid_barrel_cnt, 10000, "barrel number for uid barrel");

namespace bthread {

UinBarrel::UinBarrel() {
    _barrel_cnt = FLAGS_uid_barrel_cnt;
    _barrel_list = (bthread_queue_mutex_t *)calloc(FLAGS_uid_barrel_cnt, sizeof(bthread_queue_mutex_t));
    assert(_barrel_list);
    for(int i=0; i<FLAGS_uid_barrel_cnt; ++i) {
        if(bthread_queue_mutex_init(&_barrel_list[i],NULL) ) {
            assert(false);
        }
    }
}

UinBarrel::~UinBarrel() {
    assert(_barrel_list);
    for(uint32_t i=0; i<_barrel_cnt; ++i) {
        bthread_queue_mutex_destroy(&_barrel_list[i]);
    }
    free(_barrel_list);
}

UinBarrel* UinBarrel::GetInstance() {
    return Singleton<UinBarrel>::get();
}

int UinBarrel::LockUinBarrel(uint64_t uid) {
    uint32_t hash_idx = butil::SuperFastHash((const char *)&uid, sizeof(uid))%_barrel_cnt;
    bthread_queue_mutex_lock(&_barrel_list[hash_idx]);
    return 0;
}

int UinBarrel::UnLockUinBarrel(uint64_t uid) {
    uint32_t hash_idx = butil::SuperFastHash((const char *)&uid, sizeof(uid))%_barrel_cnt;
    bthread_queue_mutex_unlock(&_barrel_list[hash_idx]);
    return 0;
}

inline int UinBarrel::GetBarrelIdx(uint64_t uid) {
    return (butil::SuperFastHash((const char *)&uid, sizeof(uid))%_barrel_cnt);
}

} // namespace bthread
