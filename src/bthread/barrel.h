// uid barrel - a barrel library to make same uid queue up, it's some kind of serialization in brpc.
// Copyright (c) 2018 abstraction No Inc 
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
// Date: 2018/11/7

#ifndef  BTHREAD_BARREL_H
#define  BTHREAD_BARREL_H

#include "bthread/types.h"
#include "bthread/mutex.h"
#include "butil/scoped_lock.h"
#include "bvar/utils/lock_timer.h"

namespace bthread {

//all uid will hash into uid_barrel_cnt number barrels, uids in same barrel will queue by LockUinBarrel and UnLockUinBarrel
//caller can be bthread or pthread, this UinBarrel design for serialization
class UinBarrel {
public:
    UinBarrel();
    ~UinBarrel();
    static UinBarrel* GetInstance();
    
    //should only return 0
    int LockUinBarrel(uint64_t uid);
    
    int UnLockUinBarrel(uint64_t uid);

    inline int GetBarrelIdx(uint64_t uid);

private:
    uint32_t _barrel_cnt;   //deafult 10000 barrel
    bthread_queue_mutex_t *_barrel_list;
};

class bUinBarrelGaurd {
public:
    bUinBarrelGaurd(uint64_t uid) : _uid(uid) {
        if(uid>0) {
            UinBarrel::GetInstance()->LockUinBarrel(uid);
        }
    }
    ~bUinBarrelGaurd() {
        if(_uid>0) {
            UinBarrel::GetInstance()->UnLockUinBarrel(_uid);
        }
    }
private:
    uint64_t _uid;
};


} // namespace bthread

#endif  // BTHREAD_BARREL_H 

