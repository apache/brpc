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

// Date: Fri Jul 14 11:29:21 CST 2017

#ifndef  BVAR_SCOPED_TIMER_H
#define  BVAR_SCOPED_TIMER_H

#include "butil/time.h"

// Accumulate microseconds spent by scopes into bvar, useful for debugging.
// Example:
//   bvar::Adder<int64_t> g_function1_spent;
//   ...
//   void function1() {
//     // time cost by function1() will be sent to g_spent_time when
//     // the function returns.
//     bvar::ScopedTimer tm(g_function1_spent);
//     ...
//   }
// To check how many microseconds the function spend in last second, you
// can wrap the bvar within PerSecond and make it viewable from /vars
//   bvar::PerSecond<bvar::Adder<int64_t> > g_function1_spent_second(
//     "function1_spent_second", &g_function1_spent);
namespace bvar{
template <typename T>
class ScopedTimer {
public:
    explicit ScopedTimer(T& bvar)
        : _start_time(butil::cpuwide_time_us()), _bvar(&bvar) {}

    ~ScopedTimer() {
        *_bvar << (butil::cpuwide_time_us() - _start_time);
    }

    void reset() { _start_time = butil::cpuwide_time_us(); }

private:
    DISALLOW_COPY_AND_ASSIGN(ScopedTimer);
    int64_t _start_time;
    T* _bvar;
};
} // namespace bvar

#endif  //BVAR_SCOPED_TIMER_H
