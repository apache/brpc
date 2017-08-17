// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2017 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Jul 14 11:29:21 CST 2017

#ifndef  BVAR_SCOPED_TIMER_H
#define  BVAR_SCOPED_TIMER_H

#include "base/time.h"

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
        : _start_time(base::cpuwide_time_us()), _bvar(&bvar) {}

    ~ScopedTimer() {
        *_bvar << (base::cpuwide_time_us() - _start_time);
    }

    void reset() { _start_time = base::cpuwide_time_us(); }

private:
    DISALLOW_COPY_AND_ASSIGN(ScopedTimer);
    const int64_t _start_time;
    T* _bvar;
};
} // namespace bvar

#endif  //BVAR_SCOPED_TIMER_H
