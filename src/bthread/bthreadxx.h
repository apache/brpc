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

// Authors: Shuo Zang (jasonszang@126.com)

#ifndef BTHREAD_BTHREADXX_H
#define BTHREAD_BTHREADXX_H

#include <butil/macros.h>

#ifdef BUTIL_CXX11_ENABLED

#include <chrono>
#include <functional>
#include <memory>
#include <system_error>
#include <type_traits>
#include "bthread/bthread.h"
#include "bthread/condition_variable.h"
#include "bthread/mutex.h"

namespace bthread {

namespace detail {

template<typename MatchType, typename TArg>
using DisableOverload = std::enable_if<
        !std::is_base_of<
                MatchType,
                typename std::decay<TArg>::type
        >::value
>;

template<typename MatchType, typename TArg>
using DisableOverloadT = typename DisableOverload<MatchType, TArg>::type;

// Just for identifying bthread. There is a bthread_id_t but it is a totally different thing.
using bthread_id = bthread_t;

constexpr bthread_t NULL_BTHREAD = 0;

struct ThreadFunc {
    virtual ~ThreadFunc() = default;

    virtual void run() = 0;
};

template<typename Function>
struct ThreadFuncImpl : public ThreadFunc {
    explicit ThreadFuncImpl(Function&& f) : f_(std::forward<Function>(f)) {
    }

    void run() override {
        f_();
    }

    Function f_;
};

template<typename Callable>
std::unique_ptr<ThreadFunc> make_func_ptr(Callable&& f) {
    return std::unique_ptr<ThreadFunc>(new ThreadFuncImpl<Callable>(std::forward<Callable>(f)));
}

inline void* thread_func_proxy(void* owning_func_ptr) {
    std::unique_ptr<ThreadFunc> func_ptr{static_cast<ThreadFunc*>(owning_func_ptr)};
    func_ptr->run();
    return nullptr;
}

} // namespace detail

class BThreadIdWrapper;

namespace this_bthread {
    inline BThreadIdWrapper get_id() noexcept;
}

class BThreadIdWrapper {
public:
    BThreadIdWrapper() = default;

    friend bool operator==(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return lhs.id_ == rhs.id_;
    }

    friend bool operator<(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return lhs.id_ < rhs.id_;
    }

    friend bool operator!=(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return !(lhs == rhs);
    }

    friend bool operator<=(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return !(rhs < lhs);
    }

    friend bool operator>(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return rhs < lhs;
    }

    friend bool operator>=(BThreadIdWrapper lhs, BThreadIdWrapper rhs) {
        return !(lhs < rhs);
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits>& ost, BThreadIdWrapper id) {
        return ost << id.id_;
    }

    friend BThreadIdWrapper this_bthread::get_id() noexcept;
    friend class BThread;
    friend struct std::hash<BThreadIdWrapper>;

private:

    BThreadIdWrapper(bthread_t id) noexcept: id_(id) {
    }

    detail::bthread_id id_{0};
};

struct urgent_launch_tag {
};

class BThread {
public:
    using id = BThreadIdWrapper;

    using native_handle_type = bthread_t;

    BThread() noexcept = default;

    BThread(const BThread& rhs) = delete;

    BThread(BThread&& rhs) noexcept: th_(rhs.th_) {
        rhs.th_ = detail::NULL_BTHREAD;
    }

    template<typename Callable, typename... Args,
            typename = detail::DisableOverloadT<BThread, Callable>,
            typename = detail::DisableOverloadT<urgent_launch_tag, Callable>>
    explicit BThread(Callable&& f, Args&& ... args);

    template<typename Callable, typename... Args,
            typename = detail::DisableOverloadT<BThread, Callable>>
    explicit BThread(urgent_launch_tag /*tag*/, Callable&& f, Args&& ... args);

    ~BThread() {
        joinable() ? std::terminate() : void();
    }

    BThread& operator=(const BThread& rhs) = delete;

    BThread& operator=(BThread&& rhs) noexcept;

    bool joinable() const noexcept {
        return th_ != detail::NULL_BTHREAD;
    }

    id get_id() const noexcept {
        return id{th_};
    }

    native_handle_type native_handle() {
        return th_;
    }

    void join();

    void detach();

    void swap(BThread& other) noexcept {
        std::swap(th_, other.th_);
    }

private:

    template<typename Callable, typename... Args>
    BThread(bool urgent, Callable&& f, Args&& ...args);

    bthread_t th_{detail::NULL_BTHREAD};
};

template<typename Callable, typename... Args, typename, typename>
BThread::BThread(Callable&& f, Args&& ... args):
        BThread(false, std::forward<Callable>(f), std::forward<Args>(args)...) {
}

template<typename Callable, typename... Args, typename>
BThread::BThread(urgent_launch_tag /*tag*/, Callable&& f, Args&& ... args):
        BThread(true, std::forward<Callable>(f), std::forward<Args>(args)...) {
}

template<typename Callable, typename... Args>
BThread::BThread(bool urgent, Callable&& f, Args&& ... args) {
    auto thread_func_ptr = detail::make_func_ptr(
            std::bind(std::forward<Callable>(f), std::forward<Args>(args)...));
    auto start_func = urgent ? bthread_start_urgent : bthread_start_background;
    int ec = start_func(&th_, nullptr, detail::thread_func_proxy, thread_func_ptr.get());
    if (!ec) {
        thread_func_ptr.release();
    } else {
        throw std::system_error(ec, std::generic_category(), "Failed starting bthread");
    }
}

} // namespace bthread

namespace std {

template<>
struct hash<::bthread::BThreadIdWrapper> {
    using argument_type = ::bthread::BThreadIdWrapper;
    using result_type = size_t;

    size_t operator()(argument_type op) const noexcept {
        return hash<::bthread::detail::bthread_id>()(op.id_);
    }
};

} // namespace std

namespace bthread {

namespace this_bthread {

inline void yield() noexcept {
    bthread_yield();
}

// NOTE: Unlike std::this_thread::get_id() that always return a valid id, this function will
// return an id object that hold the special distinct value that does not represent any thread,
// should this function be called from outside any bthreads, e.g. a normal pthread.
inline ::bthread::BThread::id get_id() noexcept {
    return ::bthread::BThread::id{bthread_self()};
}

template<class Clock, class Duration>
void sleep_until(const std::chrono::time_point<Clock, Duration>& sleep_time) {
    ::bthread::Mutex mtx;
    ::bthread::ConditionVariable cv;
    std::unique_lock<::bthread::Mutex> lock(mtx);
    cv.wait_until(lock, sleep_time, [&sleep_time]() { return Clock::now() >= sleep_time; });
}

template<class Rep, class Period>
void sleep_for(const std::chrono::duration<Rep, Period>& sleep_duration) {
    auto sleep_time = std::chrono::steady_clock::now() + sleep_duration;
    sleep_until(sleep_time);
}

} // namespace this_bthread

} // namespace this_bthread

#endif // BUTIL_CXX11_ENABLED

#endif // BTHREAD_BTHREADXX_H
