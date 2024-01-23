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

#ifndef BRPC_COROUTINE_INL_H
#define BRPC_COROUTINE_INL_H

#include "bthread/unstable.h"   // bthread_timer_add
#include "bthread/butex.h"      // butex_wake/butex_wait

namespace brpc {
namespace experimental {

namespace detail {

class AwaitablePromiseBase {
public:
    AwaitablePromiseBase() {
    }

    virtual ~AwaitablePromiseBase() {
        delete _suspended_or_done;
    }

    virtual void resume() = 0;
    virtual void destroy() = 0;

    bool needs_suspend() {
        return _suspended_or_done != nullptr;
    }

    void set_needs_suspend() {
        _suspended_or_done = new std::atomic<bool>();
        _suspended_or_done->store(false);
    }

    // For a Coroutine's leaf function
    // Its caller will be suspended, waiting for its done.
    // But the suspend and done are always in different threads.
    // It may suspend before done, or done before suspend.
    // So we use an atomic<bool>, after first suspend_or_done() it will become true.
    // Then the second suspend_or_done(), exchange(true) will returns true.
    // Then we can safely delete this.
    void suspend_or_done() {
        if (_suspended_or_done->exchange(true)) {
            // Already suspend AND done
            if (_caller) {
                // The leaf function has finished, resume its caller.
                _caller->resume();
            }
            delete this;
        }
    }

    void on_suspend() { suspend_or_done(); }
    void on_done() { suspend_or_done(); }

    void set_callback(std::function<void()> cb) {
        _callback = cb;
    }

    void set_caller(AwaitablePromiseBase* caller) {
        _caller = caller;
    }

    // When the coroutine function begins, initial_suspend() will be called
    auto initial_suspend() {
        // Always suspend the function, later resume() will make it start to run
        return std::suspend_always{};
    }

    // When the coroutine function throws unhandled exception, unhandled_exception() will be called
    void unhandled_exception() {
        LOG(ERROR) << "Coroutine throws unhandled exception!";
        std::exit(1);
    }

    // When the coroutine function ends, final_suspend() will be called
    auto final_suspend() noexcept {
        if (_caller) {
            // The caller is waiting for this function to return
            // Now it can be resumed
            _caller->resume();
        }
        if (_callback) {
            _callback();
            _callback = nullptr;
        }
        // Returns suspend_never{} so that the coroutine will be destroyed and the AwaitablePromise be deleted.
        // DO NOT call destroy() here, which will cause double destruct of RAII objects.
        // DO NOT call delete this here, which will cause malloc and free not match.
        return std::suspend_never{};
    }

private:
    // For a Coroutine's root function, it needs a callback to notify its waiter
    std::function<void()> _callback;
    // For a Coroutine's leaf function, it is always resumed from another thread.
    // It needs an atomic variable to keep thread safety.
    // Non-leaf function does't need this, so we defined it as an optional pointer.
    std::atomic<bool>* _suspended_or_done{nullptr};
    // For a Coroutine's non-root function, it needs to resume its caller when it finished.
    AwaitablePromiseBase* _caller{nullptr};
};

template <typename T>
class AwaitablePromise : public AwaitablePromiseBase {
public:
    T value() {
        return _value;
    }

    void set_value(T value) {
        _value = value;
    }

    void resume() override {
        _coro.resume();
    }

    void destroy() override {
        _coro.destroy();
    }

    // When we call a coroutine function, an AwaitablePromise<T> will be created.
    // Then call its get_return_object() to return an Awaitable<T>.
    auto get_return_object() {
        _coro = std::coroutine_handle<AwaitablePromise>::from_promise(*this);
        return Awaitable<T>(this);
    }

    // When we call co_return in a function, return_value() will be called.
    auto return_value(T v) {
        _value = v;
        return std::suspend_never{};
    }

private:
    T _value;
    std::coroutine_handle<AwaitablePromise> _coro;
};

template <>
class AwaitablePromise<void> : public AwaitablePromiseBase {
public:
    void resume() override {
        _coro.resume();
    }

    void destroy() override {
        _coro.destroy();
    }

    // When we call a coroutine function, an AwaitablePromise<void> will be created.
    // Then call its get_return_object() to return an Awaitable<void>.
    auto get_return_object() {
        _coro = std::coroutine_handle<AwaitablePromise>::from_promise(*this);
        return Awaitable<void>(this);
    }

    // When we call return in a coroutine function, return_value() will be called.
    auto return_value() {
        return std::suspend_never{};
    }

private:
    std::coroutine_handle<AwaitablePromise> _coro;
};

} // namespace detail

// When co_await an Awaitable<T>, await_ready() will be called automatically.
template <typename T>
inline bool Awaitable<T>::await_ready() {
    // Always returns false so that the caller will be suspended at the co_await point.
    return false;
}

// If await_ready returns false, await_suspend() will be called automatically.
template <typename T>
template <typename U>
inline void Awaitable<T>::await_suspend(std::coroutine_handle<detail::AwaitablePromise<U> > awaiting) {
    _promise->set_caller(&awaiting.promise());
    if (_promise->needs_suspend()) {
        _promise->on_suspend();
        return;
    }
    _promise->resume();
}

// When the caller resumes from co_await, await_resume() will be called to get return value
template <typename T>
inline T Awaitable<T>::await_resume() {
    if constexpr (!std::is_same<T, void>::value) {
        return _promise->value();
    }
}

inline AwaitableDone::AwaitableDone()
    : _awaitable(new detail::AwaitablePromise<void>) {
    _awaitable.promise()->set_needs_suspend();
}
    
inline void AwaitableDone::Run() {
    _awaitable.promise()->on_done();
}

template <typename T>
inline Coroutine::Coroutine(Awaitable<T>&& aw, bool detach) {
    detail::AwaitablePromise<T>* origin_promise = aw.promise();
    CHECK(origin_promise);

    if (!detach) {
        // Create butex for join()
        _butex = bthread::butex_create_checked<std::atomic<int> >();
        _butex->store(0);

        // Create AwaitablePromise for awaitable()
        _promise = new detail::AwaitablePromise<T>();
        _promise->set_needs_suspend();

        auto cb = [this, origin_promise]() {
            if constexpr (!std::is_same<T, void>::value) {
                dynamic_cast<detail::AwaitablePromise<T>*>(_promise)->set_value(origin_promise->value());
            }
            // wakeup join()
            _butex->store(1);
            bthread::butex_wake(_butex);

            // wakeup co_await on awaitable()
            _promise->on_done();
        };
        origin_promise->set_callback(cb);
    }

    // Start to run the coroutine
    origin_promise->resume();
}

inline Coroutine::~Coroutine() {
    if (_promise != nullptr && !_waited) {
        join();
    }
    if (_butex) {
        bthread::butex_destroy(_butex);
        _butex = nullptr;
    }
}

template <typename T>
inline T Coroutine::join() {
    CHECK(_promise != nullptr) << "join() can not be called to detached coroutine!";
    CHECK(_waited == false) << "awaitable() or join() can only be called once!";
    _waited = true;
    bthread::butex_wait(_butex, 0, nullptr);
    if constexpr (!std::is_same<T, void>::value) {
        auto promise = dynamic_cast<detail::AwaitablePromise<T>*>(_promise);
        CHECK(promise != nullptr) << "join type not match";
        T ret = promise->value();
        _promise->on_suspend();
        return ret;
    } else {
        _promise->on_suspend();
    }
}

template <typename T>
inline Awaitable<T> Coroutine::awaitable() {
    CHECK(_promise != nullptr) << "awaitable() can not be called to detached coroutine!";
    CHECK(_waited == false) << "awaitable() or join() can only be called once!";
    auto promise = dynamic_cast<detail::AwaitablePromise<T>*>(_promise);
    CHECK(promise != nullptr) << "awaitable type not match";
    _waited = true;
    return Awaitable<T>(promise);
}

// NOTE: the caller will be resumed on bthread timer thread,
// bthread only have one timer thread, this may be performance bottle-neck
inline Awaitable<int> Coroutine::usleep(int sleep_us) {
    auto promise = new detail::AwaitablePromise<int>();
    promise->set_needs_suspend();
    bthread_timer_t timer;
    auto abstime = butil::microseconds_from_now(sleep_us);
    auto cb = [](void* p) {
        auto promise = static_cast<detail::AwaitablePromise<int>*>(p);
        promise->set_value(0);
        promise->on_done();
    };
    if (bthread_timer_add(&timer, abstime, cb, promise) != 0) {
        promise->set_value(-1);
        promise->on_done();
    }
    return Awaitable<int>(promise);
}

} // namespace experimental
} // namespace brpc

#endif // BRPC_COROUTINE_INL_H