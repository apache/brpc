// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2016/06/03 13:06:40

#ifndef  PUBLIC_BTHREAD_COUNTDOWN_EVENT_H
#define  PUBLIC_BTHREAD_COUNTDOWN_EVENT_H

#include "bthread/bthread.h"

namespace bthread {

// Represents a synchronization primitive that is signaled when its count
// reaches zero.
class CountdownEvent {
public:
    // Ctor/Dtor
    CountdownEvent();
    ~CountdownEvent();

    // Initialize the counter to 1
    int init() { return init(1); }

    // Initialize the counter to |initial_count|
    int init(int initial_count);

    // Decrease the counter by 1
    void signal() { return signal(1); }

    // Decrease the counter by |sig|
    void signal(int sig);

    // Increase current counter by 1
    void add_count() { return add_count(1); }

    // Increase current counter by |v|
    void add_count(int v);

    // Reset the counter to 1
    void reset() { return reset(1); }
    // Reset the counter to |v|
    void reset(int v);

    // Block the current thread until the counter reaches 0
    void wait();

    // Block the current thread until the counter reaches 0 or duetime has expired
    // Returns 0 on success, ETIMEDOUT otherwise.
    int timed_wait(const timespec& duetime);

private:
    char _butex_memory[BUTEX_MEMORY_SIZE];
    int *_butex;
    bool _wait_was_invoked;
};

}  // namespace bthread

#endif  //PUBLIC_BTHREAD_COUNTDOWN_EVENT_H
