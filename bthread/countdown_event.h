// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2016/06/03 13:06:40

#ifndef  PUBLIC_BTHREAD_COUNTDOWN_EVENT_H
#define  PUBLIC_BTHREAD_COUNTDOWN_EVENT_H

#include "bthread/bthread.h"

namespace bthread {

// A synchronization primitive to wait for multiple signallers.
class CountdownEvent {
public:
    CountdownEvent(int initial_count = 1);
    ~CountdownEvent();

    // Increase current counter by |v|
    void add_count(int v = 1);

    // Reset the counter to |v|
    void reset(int v = 1);

    // Decrease the counter by |sig|
    void signal(int sig = 1);

    // Block current thread until the counter reaches 0
    void wait();

    // Block the current thread until the counter reaches 0 or duetime has expired
    // Returns 0 on success, ETIMEDOUT otherwise.
    int timed_wait(const timespec& duetime);

private:
    int *_butex;
    bool _wait_was_invoked;
};

}  // namespace bthread

#endif  //PUBLIC_BTHREAD_COUNTDOWN_EVENT_H
