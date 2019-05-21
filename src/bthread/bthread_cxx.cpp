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

#include "bthread_cxx.h"

namespace bthread {

Thread& Thread::operator=(Thread&& rhs) noexcept {
    if (joinable()) {
        std::terminate();
    }
    th_ = rhs.th_;
    rhs.th_ = detail::NULL_BTHREAD;
    return *this;
}

void Thread::join() {
    int ec = EINVAL;
    if (joinable()) {
        ec = bthread_join(th_, nullptr);
        if (!ec) {
            th_ = detail::NULL_BTHREAD;
        }
    }
    if (ec) {
        throw std::system_error(ec, std::generic_category());
    }
}

void Thread::detach() {
    int ec = EINVAL;
    if (joinable()) {
        // There is no `bthread_detach' equivalent to `pthread_detach'. Since native bthreads
        // are "detached but still joinable" we only need to make our C++ bthread non-joinable
        // to implement the detaching semantic.
        ec = 0;
        th_ = detail::NULL_BTHREAD;
    }
    if (ec) {
        throw std::system_error(ec, std::generic_category());
    }
}

} // namespace bthread
