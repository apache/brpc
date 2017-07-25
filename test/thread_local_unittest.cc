// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread_local.h"
#include "base/synchronization/waitable_event.h"
#include <gtest/gtest.h>

namespace base {

namespace {

class ThreadLocalTesterBase : public base::DelegateSimpleThreadPool::Delegate {
 public:
  typedef base::ThreadLocalPointer<ThreadLocalTesterBase> TLPType;

  ThreadLocalTesterBase(TLPType* tlp, base::WaitableEvent* done)
      : tlp_(tlp),
        done_(done) {
  }
  virtual ~ThreadLocalTesterBase() {}

 protected:
  TLPType* tlp_;
  base::WaitableEvent* done_;
};

class SetThreadLocal : public ThreadLocalTesterBase {
 public:
  SetThreadLocal(TLPType* tlp, base::WaitableEvent* done)
      : ThreadLocalTesterBase(tlp, done),
        val_(NULL) {
  }
  virtual ~SetThreadLocal() {}

  void set_value(ThreadLocalTesterBase* val) { val_ = val; }

  virtual void Run() OVERRIDE {
    DCHECK(!done_->IsSignaled());
    tlp_->Set(val_);
    done_->Signal();
  }

 private:
  ThreadLocalTesterBase* val_;
};

class GetThreadLocal : public ThreadLocalTesterBase {
 public:
  GetThreadLocal(TLPType* tlp, base::WaitableEvent* done)
      : ThreadLocalTesterBase(tlp, done),
        ptr_(NULL) {
  }
  virtual ~GetThreadLocal() {}

  void set_ptr(ThreadLocalTesterBase** ptr) { ptr_ = ptr; }

  virtual void Run() OVERRIDE {
    DCHECK(!done_->IsSignaled());
    *ptr_ = tlp_->Get();
    done_->Signal();
  }

 private:
  ThreadLocalTesterBase** ptr_;
};

}  // namespace

// In this test, we start 2 threads which will access a ThreadLocalPointer.  We
// make sure the default is NULL, and the pointers are unique to the threads.
TEST(ThreadLocalTest, Pointer) {
  base::DelegateSimpleThreadPool tp1("ThreadLocalTest tp1", 1);
  base::DelegateSimpleThreadPool tp2("ThreadLocalTest tp1", 1);
  tp1.Start();
  tp2.Start();

  base::ThreadLocalPointer<ThreadLocalTesterBase> tlp;

  static ThreadLocalTesterBase* const kBogusPointer =
      reinterpret_cast<ThreadLocalTesterBase*>(0x1234);

  ThreadLocalTesterBase* tls_val;
  base::WaitableEvent done(true, false);

  GetThreadLocal getter(&tlp, &done);
  getter.set_ptr(&tls_val);

  // Check that both threads defaulted to NULL.
  tls_val = kBogusPointer;
  done.Reset();
  tp1.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(static_cast<ThreadLocalTesterBase*>(NULL), tls_val);

  tls_val = kBogusPointer;
  done.Reset();
  tp2.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(static_cast<ThreadLocalTesterBase*>(NULL), tls_val);


  SetThreadLocal setter(&tlp, &done);
  setter.set_value(kBogusPointer);

  // Have thread 1 set their pointer value to kBogusPointer.
  done.Reset();
  tp1.AddWork(&setter);
  done.Wait();

  tls_val = NULL;
  done.Reset();
  tp1.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(kBogusPointer, tls_val);

  // Make sure thread 2 is still NULL
  tls_val = kBogusPointer;
  done.Reset();
  tp2.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(static_cast<ThreadLocalTesterBase*>(NULL), tls_val);

  // Set thread 2 to kBogusPointer + 1.
  setter.set_value(kBogusPointer + 1);

  done.Reset();
  tp2.AddWork(&setter);
  done.Wait();

  tls_val = NULL;
  done.Reset();
  tp2.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(kBogusPointer + 1, tls_val);

  // Make sure thread 1 is still kBogusPointer.
  tls_val = NULL;
  done.Reset();
  tp1.AddWork(&getter);
  done.Wait();
  EXPECT_EQ(kBogusPointer, tls_val);

  tp1.JoinAll();
  tp2.JoinAll();
}

TEST(ThreadLocalTest, Boolean) {
  {
    base::ThreadLocalBoolean tlb;
    EXPECT_FALSE(tlb.Get());

    tlb.Set(false);
    EXPECT_FALSE(tlb.Get());

    tlb.Set(true);
    EXPECT_TRUE(tlb.Get());
  }

  // Our slot should have been freed, we're all reset.
  {
    base::ThreadLocalBoolean tlb;
    EXPECT_FALSE(tlb.Get());
  }
}

}  // namespace base
