/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#ifdef IO_URING_ENABLED

#include <condition_variable>
#include <butil/logging.h>
#include <iostream>
#include <liburing.h>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "brpc/socket.h"
#include "bthread/inbound_ring_buf.h"
#include "bthread/ring_write_buf_pool.h"
#include "butil/threading/platform_thread.h"
#include "spsc_queue.h"

namespace bthread {
    extern BAIDU_THREAD_LOCAL TaskGroup *tls_task_group;
}

struct RingFsyncData {
    int fd_;
    bthread::Mutex mutex_;
    bthread::ConditionVariable cv_;
    bool finish_{false};
    int res_{-1};

    int Wait() {
        std::unique_lock lk(mutex_);
        while (!finish_) {
            cv_.wait(lk);
        }
        return res_;
    }

    void Notify(int res) {
        std::unique_lock lk(mutex_);
        finish_ = true;
        res_ = res;
        cv_.notify_one();
    }
};

class RingListener {
public:
    RingListener(bthread::TaskGroup *group) : task_group_(group) {
    }

    ~RingListener();

    int Init();

    void Close() {
        if (ring_init_) {
            io_uring_queue_exit(&ring_);
            ring_init_ = false;
        }

        if (in_buf_) {
            free(in_buf_);
            in_buf_ = nullptr;
        }
    }

    int Register(brpc::Socket *sock);

    int SubmitRecv(brpc::Socket *sock);

    int SubmitFixedWrite(brpc::Socket *sock, uint16_t ring_buf_idx);

    int SubmitNonFixedWrite(brpc::Socket *sock);

    int SubmitWaitingNonFixedWrite(brpc::Socket *sock);

    int SubmitFsync(RingFsyncData *args);

    bool HasJobsToSubmit() const {
        return submit_cnt_ > 0;
    }

    bool HasTasks() const {
        return submit_cnt_ > 0 || cqe_ready_.load(std::memory_order_relaxed);
    }

    int SubmitAll();

    void Unregister(int fd) { SubmitCancel(fd); }

    void PollAndNotify();

    size_t ExtPoll();

    void ExtWakeup();

    void Run();

    void RecycleReadBuf(uint16_t bid, size_t bytes);

    const char *GetReadBuf(uint16_t bid) const {
        return in_buf_ + bid * buf_length;
    }

    std::pair<char *, uint16_t> GetWriteBuf() { return write_buf_pool_->Get(); }

    void RecycleWriteBuf(uint16_t buf_idx) { write_buf_pool_->Recycle(buf_idx); }

private:
    void FreeBuf() {
        if (in_buf_ != nullptr) {
            free(in_buf_);
        }
    }

    int SubmitCancel(int fd);

    int SubmitRegisterFile(brpc::Socket *sock, int *fd, int32_t fd_idx);

    void HandleCqe(io_uring_cqe *cqe);

    enum struct OpCode : uint8_t {
        Recv = 0,
        CancelRecv,
        RegisterFile,
        FixedWrite,
        FixedWriteFinish,
        NonFixedWrite,
        NonFixedWriteFinish,
        WaitingNonFixedWrite,
        Fsync,
        Noop = 255
    };

    uint8_t OpCodeToInt(OpCode op) {
        switch (op) {
            case OpCode::Recv:
                return 0;
            case OpCode::CancelRecv:
                return 1;
            case OpCode::RegisterFile:
                return 2;
            case OpCode::FixedWrite:
                return 3;
            case OpCode::FixedWriteFinish:
                return 4;
            case OpCode::NonFixedWrite:
                return 5;
            case OpCode::NonFixedWriteFinish:
                return 6;
            case OpCode::WaitingNonFixedWrite:
                return 7;
            case OpCode::Fsync:
                return 8;
            default:
                return UINT8_MAX;
        }
    }

    OpCode IntToOpCode(uint8_t c) {
        switch (c) {
            case 0:
                return OpCode::Recv;
            case 1:
                return OpCode::CancelRecv;
            case 2:
                return OpCode::RegisterFile;
            case 3:
                return OpCode::FixedWrite;
            case 4:
                return OpCode::FixedWriteFinish;
            case 5:
                return OpCode::NonFixedWrite;
            case 6:
                return OpCode::NonFixedWriteFinish;
            case 7:
                return OpCode::WaitingNonFixedWrite;
            case 8:
                return OpCode::Fsync;
            default:
                return OpCode::Noop;
        }
    }

    void HandleRecv(brpc::Socket *sock, io_uring_cqe *cqe);

    void HandleFixedWrite(brpc::Socket *sock, int nw, uint16_t write_buf_idx);

    void HandleBacklog();

    bool SubmitBacklog(brpc::Socket *sock, uint64_t data);

    enum struct PollStatus : uint8_t { Active = 0, Sleep, ExtPoll, Closed };

    struct io_uring ring_;
    bool ring_init_{false};
    std::atomic<PollStatus> poll_status_{PollStatus::Sleep};
    // cqe_ready_ is set by the ring listener and unset by the worker
    std::atomic<bool> cqe_ready_{false};
    uint16_t submit_cnt_{0};
    std::unordered_map<int, int> reg_fds_;
    std::mutex mux_;
    std::condition_variable cv_;
    std::thread poll_thd_;

    io_uring_buf_ring *in_buf_ring_{nullptr};
    char *in_buf_{nullptr};

    bthread::TaskGroup *task_group_{nullptr};

    eloq::SpscQueue<std::pair<brpc::Socket *, uint64_t> > waiting_socks_{
        buf_ring_size
    };
    std::atomic<uint16_t> waiting_cnt_{0};
    std::vector<std::pair<brpc::Socket *, uint64_t> > waiting_batch_{
        buf_ring_size
    };

    std::atomic<bool> has_external_{true};

    std::vector<uint16_t> free_reg_fd_idx_;

    std::unique_ptr<RingWriteBufferPool> write_buf_pool_;

    inline static size_t buf_length = sysconf(_SC_PAGESIZE);
    inline static size_t buf_ring_size = 1024;

    RingModule *ring_module_{};

    friend class RingModule;
};

#endif
