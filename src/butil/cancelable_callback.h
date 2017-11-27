// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// CancelableCallback is a wrapper around butil::Callback that allows
// cancellation of a callback. CancelableCallback takes a reference on the
// wrapped callback until this object is destroyed or Reset()/Cancel() are
// called.
//
// NOTE:
//
// Calling CancelableCallback::Cancel() brings the object back to its natural,
// default-constructed state, i.e., CancelableCallback::callback() will return
// a null callback.
//
// THREAD-SAFETY:
//
// CancelableCallback objects must be created on, posted to, cancelled on, and
// destroyed on the same thread.
//
//
// EXAMPLE USAGE:
//
// In the following example, the test is verifying that RunIntensiveTest()
// Quit()s the message loop within 4 seconds. The cancelable callback is posted
// to the message loop, the intensive test runs, the message loop is run,
// then the callback is cancelled.
//
// void TimeoutCallback(const std::string& timeout_message) {
//   FAIL() << timeout_message;
//   MessageLoop::current()->QuitWhenIdle();
// }
//
// CancelableClosure timeout(butil::Bind(&TimeoutCallback, "Test timed out."));
// MessageLoop::current()->PostDelayedTask(FROM_HERE, timeout.callback(),
//                                         4000)  // 4 seconds to run.
// RunIntensiveTest();
// MessageLoop::current()->Run();
// timeout.Cancel();  // Hopefully this is hit before the timeout callback runs.
//

#ifndef BUTIL_CANCELABLE_CALLBACK_H_
#define BUTIL_CANCELABLE_CALLBACK_H_

#include "butil/base_export.h"
#include "butil/bind.h"
#include "butil/callback.h"
#include "butil/callback_internal.h"
#include "butil/compiler_specific.h"
#include "butil/logging.h"
#include "butil/memory/weak_ptr.h"

namespace butil {

template <typename Sig>
class CancelableCallback;

template <>
class CancelableCallback<void(void)> {
 public:
  CancelableCallback() : weak_factory_(this) {}

  // |callback| must not be null.
  explicit CancelableCallback(const butil::Callback<void(void)>& callback)
      : weak_factory_(this),
        callback_(callback) {
    DCHECK(!callback.is_null());
    InitializeForwarder();
  }

  ~CancelableCallback() {}

  // Cancels and drops the reference to the wrapped callback.
  void Cancel() {
    weak_factory_.InvalidateWeakPtrs();
    forwarder_.Reset();
    callback_.Reset();
  }

  // Returns true if the wrapped callback has been cancelled.
  bool IsCancelled() const {
    return callback_.is_null();
  }

  // Sets |callback| as the closure that may be cancelled. |callback| may not
  // be null. Outstanding and any previously wrapped callbacks are cancelled.
  void Reset(const butil::Callback<void(void)>& callback) {
    DCHECK(!callback.is_null());

    // Outstanding tasks (e.g., posted to a message loop) must not be called.
    Cancel();

    // |forwarder_| is no longer valid after Cancel(), so re-bind.
    InitializeForwarder();

    callback_ = callback;
  }

  // Returns a callback that can be disabled by calling Cancel().
  const butil::Callback<void(void)>& callback() const {
    return forwarder_;
  }

 private:
  void Forward() {
    callback_.Run();
  }

  // Helper method to bind |forwarder_| using a weak pointer from
  // |weak_factory_|.
  void InitializeForwarder() {
    forwarder_ = butil::Bind(&CancelableCallback<void(void)>::Forward,
                            weak_factory_.GetWeakPtr());
  }

  // Used to ensure Forward() is not run when this object is destroyed.
  butil::WeakPtrFactory<CancelableCallback<void(void)> > weak_factory_;

  // The wrapper closure.
  butil::Callback<void(void)> forwarder_;

  // The stored closure that may be cancelled.
  butil::Callback<void(void)> callback_;

  DISALLOW_COPY_AND_ASSIGN(CancelableCallback);
};

template <typename A1>
class CancelableCallback<void(A1)> {
 public:
  CancelableCallback() : weak_factory_(this) {}

  // |callback| must not be null.
  explicit CancelableCallback(const butil::Callback<void(A1)>& callback)
      : weak_factory_(this),
        callback_(callback) {
    DCHECK(!callback.is_null());
    InitializeForwarder();
  }

  ~CancelableCallback() {}

  // Cancels and drops the reference to the wrapped callback.
  void Cancel() {
    weak_factory_.InvalidateWeakPtrs();
    forwarder_.Reset();
    callback_.Reset();
  }

  // Returns true if the wrapped callback has been cancelled.
  bool IsCancelled() const {
    return callback_.is_null();
  }

  // Sets |callback| as the closure that may be cancelled. |callback| may not
  // be null. Outstanding and any previously wrapped callbacks are cancelled.
  void Reset(const butil::Callback<void(A1)>& callback) {
    DCHECK(!callback.is_null());

    // Outstanding tasks (e.g., posted to a message loop) must not be called.
    Cancel();

    // |forwarder_| is no longer valid after Cancel(), so re-bind.
    InitializeForwarder();

    callback_ = callback;
  }

  // Returns a callback that can be disabled by calling Cancel().
  const butil::Callback<void(A1)>& callback() const {
    return forwarder_;
  }

 private:
  void Forward(A1 a1) const {
    callback_.Run(a1);
  }

  // Helper method to bind |forwarder_| using a weak pointer from
  // |weak_factory_|.
  void InitializeForwarder() {
    forwarder_ = butil::Bind(&CancelableCallback<void(A1)>::Forward,
                            weak_factory_.GetWeakPtr());
  }

  // Used to ensure Forward() is not run when this object is destroyed.
  butil::WeakPtrFactory<CancelableCallback<void(A1)> > weak_factory_;

  // The wrapper closure.
  butil::Callback<void(A1)> forwarder_;

  // The stored closure that may be cancelled.
  butil::Callback<void(A1)> callback_;

  DISALLOW_COPY_AND_ASSIGN(CancelableCallback);
};

template <typename A1, typename A2>
class CancelableCallback<void(A1, A2)> {
 public:
  CancelableCallback() : weak_factory_(this) {}

  // |callback| must not be null.
  explicit CancelableCallback(const butil::Callback<void(A1, A2)>& callback)
      : weak_factory_(this),
        callback_(callback) {
    DCHECK(!callback.is_null());
    InitializeForwarder();
  }

  ~CancelableCallback() {}

  // Cancels and drops the reference to the wrapped callback.
  void Cancel() {
    weak_factory_.InvalidateWeakPtrs();
    forwarder_.Reset();
    callback_.Reset();
  }

  // Returns true if the wrapped callback has been cancelled.
  bool IsCancelled() const {
    return callback_.is_null();
  }

  // Sets |callback| as the closure that may be cancelled. |callback| may not
  // be null. Outstanding and any previously wrapped callbacks are cancelled.
  void Reset(const butil::Callback<void(A1, A2)>& callback) {
    DCHECK(!callback.is_null());

    // Outstanding tasks (e.g., posted to a message loop) must not be called.
    Cancel();

    // |forwarder_| is no longer valid after Cancel(), so re-bind.
    InitializeForwarder();

    callback_ = callback;
  }

  // Returns a callback that can be disabled by calling Cancel().
  const butil::Callback<void(A1, A2)>& callback() const {
    return forwarder_;
  }

 private:
  void Forward(A1 a1, A2 a2) const {
    callback_.Run(a1, a2);
  }

  // Helper method to bind |forwarder_| using a weak pointer from
  // |weak_factory_|.
  void InitializeForwarder() {
    forwarder_ = butil::Bind(&CancelableCallback<void(A1, A2)>::Forward,
                            weak_factory_.GetWeakPtr());
  }

  // Used to ensure Forward() is not run when this object is destroyed.
  butil::WeakPtrFactory<CancelableCallback<void(A1, A2)> > weak_factory_;

  // The wrapper closure.
  butil::Callback<void(A1, A2)> forwarder_;

  // The stored closure that may be cancelled.
  butil::Callback<void(A1, A2)> callback_;

  DISALLOW_COPY_AND_ASSIGN(CancelableCallback);
};

typedef CancelableCallback<void(void)> CancelableClosure;

}  // namespace butil

#endif  // BUTIL_CANCELABLE_CALLBACK_H_
