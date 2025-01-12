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

// This file contains inlined implementation of socket.h

#ifndef BRPC_SOCKET_INL_H
#define BRPC_SOCKET_INL_H


namespace brpc {

inline bool Socket::MoreReadEvents(int* progress) {
    // Fail to CAS means that new events arrived.
    return !_nevent.compare_exchange_strong(
        *progress, 0, butil::memory_order_release,
            butil::memory_order_acquire);
}

inline void Socket::SetLogOff() {
    if (!_logoff_flag.exchange(true, butil::memory_order_relaxed)) {
        if (fd() < 0) {
            // This socket hasn't been connected before (such as
            // short connection), so it won't receive any epoll
            // events. We need to `SetFailed' it to trigger health
            // checking, otherwise it may be blocked forever
            SetFailed(ELOGOFF, "The server at %s is stopping",
                      butil::endpoint2str(remote_side()).c_str());
        }
    }
}

inline bool Socket::IsAvailable() const {
    return !_logoff_flag.load(butil::memory_order_relaxed) &&
        (_ninflight_app_health_check.load(butil::memory_order_relaxed) == 0);
}

static const uint32_t EOF_FLAG = (1 << 31);

inline void Socket::PostponeEOF() {
    if (CreatedByConnect()) { // not needed at server-side
        _ninprocess.fetch_add(1, butil::memory_order_relaxed);
    }
}

inline void Socket::CheckEOF() {
    if (CreatedByConnect()) { // not needed at server-side
        CheckEOFInternal();
    }
}

inline void Socket::CheckEOFInternal() {
    uint32_t nref = _ninprocess.fetch_sub(1, butil::memory_order_release);
    if ((nref & ~EOF_FLAG) == 1) {
        butil::atomic_thread_fence(butil::memory_order_acquire);
        // It's safe to call `SetFailed' each time `_ninprocess' hits 0
        SetFailed(EEOF, "Got EOF of %s", description().c_str());
    }
}

inline void Socket::SetEOF() {
    uint32_t nref = _ninprocess.fetch_or(EOF_FLAG, butil::memory_order_relaxed);
    if ((nref & EOF_FLAG) == 0) {
        // Release the additional reference in `_ninprocess'
        CheckEOFInternal();
    }
}

inline void Socket::reset_parsing_context(Destroyable* new_context) {
    Destroyable* old_ctx = _parsing_context.exchange(
        new_context, butil::memory_order_acq_rel);
    if (old_ctx) {
        old_ctx->Destroy();
    }
}

inline Destroyable* Socket::release_parsing_context() {
    return _parsing_context.exchange(NULL, butil::memory_order_acquire);
}

template <typename T>
bool Socket::initialize_parsing_context(T** ctx) {
    Destroyable* expected = NULL;
    if (_parsing_context.compare_exchange_strong(
            expected, *ctx, butil::memory_order_acq_rel,
            butil::memory_order_acquire)) {
        return true;
    } else {
        (*ctx)->Destroy();
        *ctx = static_cast<T*>(expected);
        return false;
    }
}

// NOTE: Push/Pop may be called from different threads simultaneously.
inline void Socket::PushPipelinedInfo(const PipelinedInfo& pi) {
    BAIDU_SCOPED_LOCK(_pipeline_mutex);
    if (_pipeline_q == NULL) {
        _pipeline_q = new std::deque<PipelinedInfo>;
    }
    _pipeline_q->push_back(pi);
}

inline bool Socket::PopPipelinedInfo(PipelinedInfo* info) {
    BAIDU_SCOPED_LOCK(_pipeline_mutex);
    if (_pipeline_q != NULL && !_pipeline_q->empty()) {
        *info = _pipeline_q->front();
        _pipeline_q->pop_front();
        return true;
    }
    return false;
}

inline void Socket::GivebackPipelinedInfo(const PipelinedInfo& pi) {
    BAIDU_SCOPED_LOCK(_pipeline_mutex);
    if (_pipeline_q != NULL) {
        _pipeline_q->push_front(pi);
    }
}

inline bool Socket::ValidFileDescriptor(int fd) {
    return fd >= 0 && fd != STREAM_FAKE_FD;
}

inline Socket::SharedPart* Socket::GetSharedPart() const {
    return _shared_part.load(butil::memory_order_consume);
}

inline Socket::SharedPart* Socket::GetOrNewSharedPart() {
    SharedPart* shared_part = GetSharedPart();
    if (shared_part != NULL) { // most cases
        return shared_part;
    }
    return GetOrNewSharedPartSlower();
}

} // namespace brpc


#endif  // BRPC_SOCKET_INL_H
