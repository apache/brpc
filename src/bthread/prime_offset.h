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

#ifndef BTHREAD_PRIME_OFFSET_H
#define BTHREAD_PRIME_OFFSET_H

#include "butil/fast_rand.h"
#include "butil/macros.h"

namespace bthread {
// Prime number offset for hash function.
inline size_t prime_offset(size_t seed) {
    uint32_t offsets[] = {
        #include "bthread/offset_inl.list"
    };
    return offsets[seed % ARRAY_SIZE(offsets)];
}

inline size_t prime_offset() {
    return prime_offset(butil::fast_rand());
}
}


#endif // BTHREAD_PRIME_OFFSET_H