// spin lock - user lib spin lock.caller should pass lock when init
// carefully use this,as conflict will cause high cost
// Copyright (c) 2018 Bigo, Inc.
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

// Author: hairet (hairet@vip.qq.com)
// Date: Tue Nov 6 17:30:12 CST 2018

#ifndef BTHREAD_SPIN_LOCK_H
#define BTHREAD_SPIN_LOCK_H

#include "butil/macros.h"
#include "butil/atomicops.h"

inline void b_spin_lock_init(butil::static_atomic<unsigned char> &m) {
    m.exchange(0, butil::memory_order_relaxed);
}

inline void b_spin_lock(butil::static_atomic<unsigned char> &m) {
    while(m.exchange(1, butil::memory_order_acquire));
    //FIXME!!later add sample
}

inline bool b_spin_try_lock(butil::static_atomic<unsigned char> &m) {
    return !m.exchange(1, butil::memory_order_acquire);
}

inline void b_spin_unlock(butil::static_atomic<unsigned char> &m) {
    m.exchange(0, butil::memory_order_release);
}

class bSpinLockGaurd {
public:
    bSpinLockGaurd(butil::static_atomic<unsigned char> &m) : m_lock(&m) {
        b_spin_lock(*m_lock);
    }
    ~bSpinLockGaurd() {
        b_spin_unlock(*m_lock);
    }
private:
    butil::static_atomic<unsigned char> *m_lock; 
};


#endif  //BTHREAD_SPIN_LOCK_H
