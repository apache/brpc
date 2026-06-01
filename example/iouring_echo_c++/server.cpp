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

// An echo server demonstrating all io_uring transport modes supported by bRPC.
//
// ---------------------------------------------------------------------------
// Design: one-thread-per-ring polling mode
// ---------------------------------------------------------------------------
//
// In this mode a dedicated Poller bthread drives the io_uring ring for each
// bthread_tag.  All SQ operations (SubmitRead, CutFromIOBufList) execute on
// that Poller thread, so no locking is needed on the hot I/O path.
//
// ---------------------------------------------------------------------------
// Quick-start examples
// ---------------------------------------------------------------------------
//
//   # Default: epoll (no io_uring)
//   ./echo_server
//
//   # io_uring, interrupt-driven Poller (none mode, 1 ms timeout between peeks)
//   ./echo_server --use_iouring
//
//   # io_uring + SQPOLL mode (lowest latency; needs CAP_SYS_NICE / root)
//   ./echo_server --use_iouring --iouring_polling_mode=sqpoll
//
//   # io_uring + hybrid mode (busy-spin N times then block)
//   ./echo_server --use_iouring --iouring_polling_mode=hybrid
//
//   # io_uring + zero-copy registered buffers (highest throughput)
//   ./echo_server --use_iouring --iouring_register_buffers
//
//   # io_uring + SQPOLL + zero-copy (latency + throughput optimised)
//   ./echo_server --use_iouring --iouring_polling_mode=sqpoll \
//                 --iouring_register_buffers
//
// ---------------------------------------------------------------------------
// Tunables (all --iouring_* flags are forwarded to the transport layer)
// ---------------------------------------------------------------------------
//
// Ring sizing
//   --iouring_sq_size=N       SQ depth per ring (default 256).
//   --iouring_cq_size=N       CQ depth per ring (0 = 2 * sq_size).
//
// Poller thread
//   --iouring_poller_yield    Yield after each poll iteration (reduces CPU,
//                             increases tail latency).
//   --iouring_max_cqe_poll_once=N  Max CQEs reaped per peek call (default 32).
//
// Polling mode (selects ring setup flags and CQE-reap strategy)
//   --iouring_polling_mode=   none    No kernel-side polling; Poller waits up
//                                     to 1 ms between peeks (default).
//                             sqpoll  Kernel SQ polling thread (IORING_SETUP_SQPOLL).
//                             iopoll  Block-device completion polling (O_DIRECT only).
//                             hybrid  Busy-spin N times then block.
//   --iouring_sqpoll_idle_ms  SQPOLL kernel thread idle timeout ms (default 2000).
//   --iouring_sqpoll_cpu=N    CPU to pin the SQPOLL thread (-1 = no pin).
//   --iouring_hybrid_spin_count=N  Spin iterations for hybrid mode (default 1000).
//
// Registered buffers (zero-copy)
//   --iouring_register_buffers      Enable READ_FIXED / WRITE_FIXED.
//   --iouring_mem_pool_initial_mb=N Initial registered memory (default 256 MiB).
//   --iouring_mem_pool_increase_mb=N Growth step (default 256 MiB).
//   --iouring_mem_pool_max_regions=N Max growth regions (default 8).
//   --iouring_iobuf_block_size=N    IOBuf block / slot size in bytes (default 8192).
//   --iouring_read_slot_num=N       Initial read slots per ring (default 256).
//   --iouring_read_slot_max=N       Max read slots per ring (default 4096).
// ---------------------------------------------------------------------------

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/iouring/iouring_helper.h>
#include "echo.pb.h"

DEFINE_bool(use_iouring, false,
            "Enable io_uring transport. Requires kernel >= 5.1. "
            "When false the default epoll transport is used.");

DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_string(listen_addr, "", "Server listen address, may be IPV4/IPV6/UDS. "
              "If set, --port is ignored.");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");

namespace example {
class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    virtual ~EchoServiceImpl() {}
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        response->set_message(request->message());
    }
};
}  // namespace example

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

#if BRPC_WITH_IOURING
    if (FLAGS_use_iouring) {
        // GlobalIouringInitializeOrDie() probes kernel opcodes and, when
        // --iouring_register_buffers is set, initialises the global memory
        // pool so that all subsequent IOBuf allocations come from pre-
        // registered pages.  Must be called before Server::Start().
        brpc::iouring::GlobalIouringInitializeOrDie();

        // InitPollingModeWithTag() creates the io_uring ring(s) for bthread
        // tag 0 (the default tag used by all worker bthreads) and starts the
        // Poller bthread(s).
        //
        // The Poller thread drives the ring: it dequeues ADD/REMOVE ops,
        // issues the first SubmitRead for each new connection, and reaps CQEs
        // in a tight loop.  Because every SQ operation runs on the Poller
        // thread, the ring needs no locking.
        //
        // Optional callbacks (all nullptr here):
        //   callback   – called after every PollCq pass; use h.Submit() to
        //                drain your own CQEs and queue new SQEs.
        //   init_fn    – called once after the ring is created.
        //   release_fn – called just before the ring is destroyed on shutdown.
        if (!brpc::iouring::InitPollingModeWithTag(/*tag=*/0)) {
            LOG(ERROR) << "Fail to init io_uring polling mode";
            return -1;
        }
    }
#else
    if (FLAGS_use_iouring) {
        LOG(ERROR) << "This binary was not compiled with io_uring support "
                      "(BRPC_WITH_IOURING is not set). "
                      "Rebuild with -DWITH_IOURING=ON.";
        return -1;
    }
#endif

    brpc::Server server;

    example::EchoServiceImpl echo_service_impl;
    if (server.AddService(&echo_service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    butil::EndPoint point;
    if (!FLAGS_listen_addr.empty()) {
        if (butil::str2endpoint(FLAGS_listen_addr.c_str(), &point) < 0) {
            LOG(ERROR) << "Invalid listen address: " << FLAGS_listen_addr;
            return -1;
        }
    } else {
        point = butil::EndPoint(butil::IP_ANY, FLAGS_port);
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    // No socket_mode to set here: io_uring is not a distinct transport
    // medium like RDMA/UBRING. Every plain-TCP socket created by this
    // Server automatically picks up io_uring (see TcpTransport::Init())
    // once GlobalIouringInitializeOrDie() / InitPollingModeWithTag() above
    // have brought the global io_uring context up.
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    server.RunUntilAskedToQuit();
    return 0;
}
