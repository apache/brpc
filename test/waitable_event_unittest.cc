// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/synchronization/waitable_event.h"

#include "butil/compiler_specific.h"
#include "butil/threading/platform_thread.h"
#include "butil/time/time.h"
#include <gtest/gtest.h>

namespace butil {

TEST(WaitableEventTest, ManualBasics) {
  WaitableEvent event(true, false);

  EXPECT_FALSE(event.IsSignaled());

  event.Signal();
  EXPECT_TRUE(event.IsSignaled());
  EXPECT_TRUE(event.IsSignaled());

  event.Reset();
  EXPECT_FALSE(event.IsSignaled());
  EXPECT_FALSE(event.TimedWait(TimeDelta::FromMilliseconds(10)));

  event.Signal();
  event.Wait();
  EXPECT_TRUE(event.TimedWait(TimeDelta::FromMilliseconds(10)));
}

TEST(WaitableEventTest, AutoBasics) {
  WaitableEvent event(false, false);

  EXPECT_FALSE(event.IsSignaled());

  event.Signal();
  EXPECT_TRUE(event.IsSignaled());
  EXPECT_FALSE(event.IsSignaled());

  event.Reset();
  EXPECT_FALSE(event.IsSignaled());
  EXPECT_FALSE(event.TimedWait(TimeDelta::FromMilliseconds(10)));

  event.Signal();
  event.Wait();
  EXPECT_FALSE(event.TimedWait(TimeDelta::FromMilliseconds(10)));

  event.Signal();
  EXPECT_TRUE(event.TimedWait(TimeDelta::FromMilliseconds(10)));
}

TEST(WaitableEventTest, WaitManyShortcut) {
  WaitableEvent* ev[5];
  for (unsigned i = 0; i < 5; ++i)
    ev[i] = new WaitableEvent(false, false);

  ev[3]->Signal();
  EXPECT_EQ(WaitableEvent::WaitMany(ev, 5), 3u);

  ev[3]->Signal();
  EXPECT_EQ(WaitableEvent::WaitMany(ev, 5), 3u);

  ev[4]->Signal();
  EXPECT_EQ(WaitableEvent::WaitMany(ev, 5), 4u);

  ev[0]->Signal();
  EXPECT_EQ(WaitableEvent::WaitMany(ev, 5), 0u);

  for (unsigned i = 0; i < 5; ++i)
    delete ev[i];
}

class WaitableEventSignaler : public PlatformThread::Delegate {
 public:
  WaitableEventSignaler(double seconds, WaitableEvent* ev)
      : seconds_(seconds),
        ev_(ev) {
  }

  virtual void ThreadMain() OVERRIDE {
    PlatformThread::Sleep(TimeDelta::FromSeconds(static_cast<int>(seconds_)));
    ev_->Signal();
  }

 private:
  const double seconds_;
  WaitableEvent *const ev_;
};

TEST(WaitableEventTest, WaitMany) {
  WaitableEvent* ev[5];
  for (unsigned i = 0; i < 5; ++i)
    ev[i] = new WaitableEvent(false, false);

  WaitableEventSignaler signaler(0.1, ev[2]);
  PlatformThreadHandle thread;
  PlatformThread::Create(0, &signaler, &thread);

  EXPECT_EQ(WaitableEvent::WaitMany(ev, 5), 2u);

  PlatformThread::Join(thread);

  for (unsigned i = 0; i < 5; ++i)
    delete ev[i];
}

}  // namespace butil
