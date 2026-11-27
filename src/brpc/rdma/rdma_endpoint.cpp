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

#if BRPC_WITH_RDMA

#include <algorithm>                            // std::min
#include <gflags/gflags.h>

#include <memory>
#include "butil/fd_guard.h"
#include "butil/logging.h"                   // CHECK, LOG
#include "bthread/bthread.h"
#include "brpc/errno.pb.h"
#include "brpc/socket.h"
#include "brpc/transport.h"
#include "brpc/reloadable_flags.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/srq_worker.h"
#include "brpc/rdma/rq_connection.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
namespace rdma {

extern bool g_skip_rdma_init;
// Fault injection for hardware-free handshake tests.
bool g_fail_resource_alloc_for_test = false;

DEFINE_bool(rdma_recv_zerocopy, true, "Enable zerocopy for receive side");
DEFINE_int32(rdma_zerocopy_min_size, 512, "The minimal size for receive zerocopy");
DEFINE_bool(rdma_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(rdma_trace_verbose, brpc::PassValidate);
DEFINE_bool(rdma_disable_bthread, false, "Disable bthread in RDMA");

RdmaEndpoint::RdmaEndpoint(Socket* s)
    : _socket(s)
    , _local_info(DefaultRdmaLocalInfo(RdmaQueueMode::PRIVATE_RQ)) {
    _input_processor.Init(s, InputMessengerProcessor::STREAM_RDMA_QP);
}

RdmaEndpoint::~RdmaEndpoint() {
    Reset();
}

void RdmaEndpoint::Reset() {
    DeallocateResources();
    SelectQueueMode(RdmaQueueMode::PRIVATE_RQ);
    _outgoing_ece.reset();
    _outgoing_mtu.reset();
    _input_processor.Reset();
}

void RdmaEndpoint::SelectQueueMode(RdmaQueueMode mode) {
    _queue_mode = mode;
    _local_info = DefaultRdmaLocalInfo(mode);
}

void RdmaEndpoint::GetLocalInfo(RdmaLocalInfo* info) const {
    if (_connection) {
        _connection->GetLocalInfo(info);
    } else {
        *info = _local_info;
        info->block_size = g_rdma_recv_block_size;
    }
}

void RdmaEndpoint::GetLocalConnectionInfo(ParsedHello* local) const {
    RdmaLocalInfo info;
    GetLocalInfo(&info);
    local->block_size = info.block_size;
    local->sq_size = info.sq_size;
    local->rq_size = info.rq_size;
    local->lid = GetRdmaLid();
    local->gid = GetRdmaGid();
    local->qp_num = info.qp_num;
    local->srq_protocol = info.srq_protocol;
    local->fc_window = info.fc_window;
    local->path_mtu = _outgoing_mtu;
    local->ece = _outgoing_ece;
}

int RdmaEndpoint::QueryLocalEce(ibv_ece* ece) const {
    if (!_connection) {
        return 1;
    }
    return _connection->QueryEce(ece) == 0 ? 0 : -1;
}

void RdmaEndpoint::SetOutgoingEce(const ibv_ece& ece) {
    _outgoing_ece = ece;
}

static int CheckConnectDeadline(Socket* socket, int64_t deadline_us) {
    if (socket->Failed()) {
        errno = ECANCELED;
        return -1;
    }
    if (deadline_us >= 0 && butil::gettimeofday_us() >= deadline_us) {
        errno = ETIMEDOUT;
        return -1;
    }
    return 0;
}

int RdmaEndpoint::ReconnectForHandshake() {
    Socket* s = _socket;
    if (CheckConnectDeadline(s, s->_connect_deadline_us) != 0) {
        return -1;
    }
    timespec deadline = butil::microseconds_to_timespec(s->_connect_deadline_us);
    // Use Socket::Connect to preserve the configured device/local address and
    // socket options. Keep ownership of the old fd until replacement succeeds.
    butil::fd_guard new_fd(s->Connect(
        s->_connect_deadline_us >= 0 ? &deadline : nullptr,
        nullptr, nullptr, /*check_failed=*/true));
    if (new_fd < 0 ||
        CheckConnectDeadline(s, s->_connect_deadline_us) != 0) {
        return -1;
    }
    if (ReplaceHandshakeFd(new_fd.release()) != 0) {
        return -1;
    }
    _outgoing_ece.reset();
    _outgoing_mtu.reset();
    return 0;
}

int RdmaEndpoint::ReplaceHandshakeFd(int fd) {
    Socket* s = _socket;
    butil::fd_guard new_fd(fd);
    int old_fd = s->fd();
    if (s->_transport->HasOnEdgeTrigger()) {
        s->_io_event.RemoveConsumer(old_fd);
    }
    if (s->ResetFileDescriptor(new_fd) != 0) {
        // ResetFileDescriptor clears `_fd` on error. Retain the old fd so normal
        // Socket recycling closes it and balances the connection counter.
        s->_fd.store(old_fd, butil::memory_order_release);
        return -1;
    }
    // SetFailed() can race with the earlier checks or fd setup. Validate
    // after registration, before committing ownership of the new connection.
    // The handshake's Socket reference prevents recycling or health-check
    // reset while we undo the replacement.
    if (CheckConnectDeadline(s, s->_connect_deadline_us) != 0) {
        int saved_errno = errno;
        if (s->_transport->HasOnEdgeTrigger()) {
            s->_io_event.RemoveConsumer(new_fd);
        }
        s->_fd.store(old_fd, butil::memory_order_release);
        new_fd.reset(-1);
        errno = saved_errno;
        return -1;
    }
    new_fd.release();
    close(old_fd);
    return 0;
}

bool RdmaEndpoint::IsWritable() const {
    return _connection && _connection->IsWritable();
}

ssize_t RdmaEndpoint::CutFromIOBufList(butil::IOBuf** data, size_t count) {
    if (!_connection) {
        errno = g_skip_rdma_init ? EAGAIN : ENOTCONN;
        return -1;
    }
    return _connection->Send(data, count);
}

int RdmaEndpoint::AllocateResources() {
    CHECK(!_connection);
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        if (g_fail_resource_alloc_for_test) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    _connection = CreateRdmaConnection(
        _socket, _queue_mode, _socket->_io_event.bthread_tag());
    return _connection ? 0 : -1;
}

void RdmaEndpoint::DeallocateResources() {
    // Detach before Close can release references or schedule late callbacks.
    auto connection = std::move(_connection);
    if (connection) {
        connection->Detach();
    }
}

int RdmaEndpoint::StartCqEvents() {
    if (InputMessengerProcessor::STREAM_NONE != _socket->parsing_stream_type()) {
        LOG(WARNING) << "StartCqEvents() called while " << *_socket << " is parsing";
        errno = ERDMA;
        return -1;
    }

    if (!_connection) {
        if (g_skip_rdma_init) {
            return 0;
        }
        errno = ENOTCONN;
        return -1;
    }
    return _connection->EnableReceive();
}

int RdmaEndpoint::BringUpQp(const ParsedHello& remote, bool is_server) {
    // MTU negotiation (server side): compute the minimum MTU and store it
    // so FillLocalRdmaHello can advertise the negotiated value in the server
    // hello reply. This is done before the `g_skip_rdma_init` early return so
    // that UT (which skips real QP bring-up) still sets `_outgoing_mtu`.
    if (is_server && remote.path_mtu.has_value()) {
        uint32_t local_mtu = GetRdmaActiveMtu();
        _outgoing_mtu = std::min(local_mtu, *remote.path_mtu);
    }

    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        return 0;
    }
    return _connection->Activate(remote, is_server, &_outgoing_ece);
}

bool RdmaEndpoint::OwnsConnection(const RdmaConnection* connection) const {
    // Delivery retains the owning Socket. Recycling and health-check Reset
    // wait for that reference, while handshake fallback detaches before CQ
    // delivery is enabled. Thus `_connection` cannot be detached concurrently.
    return connection && _connection.get() == connection;
}

int RdmaEndpoint::Deliver(RdmaConnection* source, butil::IOBuf* data,
                          ssize_t bytes, InputMessageClosure& last_msg) {
    if (!OwnsConnection(source)) {
        errno = ENOTCONN;
        return -1;
    }
    _input_processor.read_buf().append(data->movable());
    const int64_t received_us = butil::cpuwide_time_us();
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    return _input_processor.ProcessNewMessage(
        bytes, false, received_us, base_realtime, last_msg);
}

void RdmaEndpoint::DebugInfo(std::ostream& os, butil::StringPiece connector) const {
    os << "rdma_state=ON"
       << connector << "rdma_read_buf=" << _input_processor.read_buf().size();
    if (_connection != nullptr) {
        os << connector;
        _connection->Debug(os, connector);
    }
}

int RdmaEndpoint::GlobalInitialize() {
    if (!ValidateSrqOptions()) {
        errno = EINVAL;
        return -1;
    }
    g_rdma_recv_block_size = GetRdmaBlockSize() - IOBUF_BLOCK_HEADER_LEN;
    if (g_rdma_recv_block_size <= 0) {
        LOG(ERROR) << "rdma_recv_block_type incorrect "
                   << "(valid value: default/large/huge)";
        errno = EINVAL;
        return -1;
    }
    return RqConnection::GlobalInitialize();
}

void RdmaEndpoint::GlobalRelease() {
    StopAllSrqWorkers();
    RqConnection::GlobalRelease();
}

int RdmaEndpoint::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    return RqConnection::PollingModeInitialize(tag, std::move(callback),
                                               std::move(init_fn),
                                               std::move(release_fn));
}

void RdmaEndpoint::PollingModeRelease(bthread_tag_t tag) {
    RqConnection::PollingModeRelease(tag);
}

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
