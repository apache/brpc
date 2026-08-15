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

// Link-time mock of the URMA user-level C API, modeled on Mooncake's
// mock_urma.cpp. It compiles against upstream UMDK headers and is linked into
// brpc when liburma.so is unavailable. It lets CI run UrmaTransport unit tests
// without URMA hardware.
//
// Design (same as Mooncake):
// - Link-time substitution: the symbols are literally named urma_create_jfc
//   etc.; the linker picks this TU when liburma is absent.
// - Per-object state is held in anonymous-namespace maps keyed on the opaque
//   pointer returned to the caller. Membership is checked on delete so misuse
//   returns URMA_EINVAL rather than crashes.
// - Completion path: posted recv WRs remain pending until a SEND targets the
//   corresponding mock jetty. Payload and immediate data are copied into the
//   remote recv WR and both local-send and remote-recv completions are queued.
// - Device-name contract: device->name == "mock_urma_device" so tests can
//   match it with --urma_device=mock_urma_device.

#if BRPC_WITH_URMA

#include "urma_api.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

namespace {

struct JfcState {
    std::mutex mutex;
    std::deque<urma_cr_t> completions;
    bool event_pending{false};
};

struct PendingRecv {
    uint64_t addr;
    uint32_t len;
    uint64_t user_ctx;
};

std::shared_mutex g_rw_mutex;
bool initialized = false;
std::vector<urma_device_t *> device_list;
std::map<urma_context_t *, int> context_map;
std::map<urma_jfce_t *, int> jfce_map;
std::map<urma_jfc_t *, JfcState *> jfc_state_map;
std::map<urma_jfr_t *, int> jfr_map;
// Side-table: JFR -> the JFC it was created with (used to route recv
// completions to the right JfcState, since the JFR is an opaque handle).
std::map<urma_jfr_t *, urma_jfc_t *> jfr_jfc_map;
std::map<urma_jfr_t *, std::deque<PendingRecv>> jfr_recv_map;
std::map<urma_target_seg_t *, int> seg_map;
std::map<urma_jetty_t *, int> jetty_map;
std::map<uint32_t, urma_jetty_t *> jetty_id_map;
std::map<urma_target_jetty_t *, int> target_jetty_map;
std::atomic<uint32_t> next_jetty_id{1};

void PushCompletion(urma_jfc_t* jfc, JfcState* state,
                    const urma_cr_t& completion) {
    bool signal = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->completions.push_back(completion);
        if (!state->event_pending) {
            state->event_pending = true;
            signal = true;
        }
    }
    if (signal && jfc && jfc->jfc_cfg.jfce &&
        jfc->jfc_cfg.jfce->fd >= 0) {
        uint64_t one = 1;
        (void)write(jfc->jfc_cfg.jfce->fd, &one, sizeof(one));
    }
}

urma_device_attr_t mock_device_attr = {
    .guid = {.raw = {10}},
    .dev_cap = {},
    .port_cnt = 1,
    .port_attr = {{.max_mtu = URMA_MTU_4096,
                   .state = URMA_PORT_ACTIVE,
                   .active_width = URMA_LINK_X1,
                   .active_speed = URMA_SP_100G,
                   .active_mtu = URMA_MTU_4096}},
    .reserved_jetty_id_min = 0,
    .reserved_jetty_id_max = 1024};

urma_eid_info_t mock_eid_info = {
    .eid = {{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
             0x0C, 0x0D, 0x0E, 0x0F, 0x10}},
    .eid_index = 0};

}  // namespace

extern "C" {

urma_status_t urma_init(urma_init_attr_t *init_attr) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (initialized) {
        return URMA_EEXIST;
    }
    initialized = true;
    return URMA_SUCCESS;
}

urma_status_t urma_uninit(void) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    initialized = false;
    for (auto device : device_list) {
        delete device;
    }
    device_list.clear();
    context_map.clear();
    jfce_map.clear();
    for (auto &kv : jfc_state_map) {
        delete kv.second;
    }
    jfc_state_map.clear();
    jfr_map.clear();
    jfr_jfc_map.clear();
    jfr_recv_map.clear();
    seg_map.clear();
    jetty_map.clear();
    jetty_id_map.clear();
    target_jetty_map.clear();
    next_jetty_id.store(1);
    return URMA_SUCCESS;
}

urma_device_t **urma_get_device_list(int *num_devices) {
    {
        std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
        if (!initialized) {
            *num_devices = 0;
            return nullptr;
        }
        if (!device_list.empty()) {
            *num_devices = device_list.size();
            urma_device_t **devices = new urma_device_t *[device_list.size()];
            for (size_t i = 0; i < device_list.size(); ++i) {
                devices[i] = device_list[i];
            }
            return devices;
        }
    }
    {
        std::unique_lock<std::shared_mutex> write_lock(g_rw_mutex);
        if (!initialized) {
            *num_devices = 0;
            return nullptr;
        }
        if (device_list.empty()) {
            urma_device_t *device = new urma_device_t;
            strcpy(device->name, "mock_urma_device");
            strcpy(device->path, "/sys/class/infiniband/mock_device");
            device->type = URMA_TRANSPORT_UB;
            device->ops = nullptr;
            device->sysfs_dev = nullptr;
            device_list.push_back(device);
        }
        *num_devices = device_list.size();
        urma_device_t **devices = new urma_device_t *[device_list.size()];
        for (size_t i = 0; i < device_list.size(); ++i) {
            devices[i] = device_list[i];
        }
        return devices;
    }
}

urma_device_t *urma_get_device_by_name(char *dev_name) {
    {
        std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
        if (!initialized) {
            return nullptr;
        }
        if (!device_list.empty()) {
            for (auto device : device_list) {
                if (strcmp(device->name, dev_name) == 0) {
                    return device;
                }
            }
            return device_list[0];
        }
    }
    {
        std::unique_lock<std::shared_mutex> write_lock(g_rw_mutex);
        if (!initialized) {
            return nullptr;
        }
        if (device_list.empty()) {
            auto *device = new urma_device_t;
            strcpy(device->name, "mock_urma_device");
            strcpy(device->path, "/sys/class/infiniband/mock_device");
            device->type = URMA_TRANSPORT_UB;
            device->ops = nullptr;
            device->sysfs_dev = nullptr;
            device_list.push_back(device);
        }
        for (auto device : device_list) {
            if (strcmp(device->name, dev_name) == 0) {
                return device;
            }
        }
        return device_list.empty() ? nullptr : device_list[0];
    }
}

void urma_free_device_list(urma_device_t **device_list) {
    if (device_list) {
        delete[] device_list;
    }
}

urma_status_t urma_query_device(urma_device_t *device,
                                urma_device_attr_t *attr) {
    if (!device || !attr) {
        return URMA_EINVAL;
    }
    mock_device_attr.dev_cap.max_jfc = 1024;
    mock_device_attr.dev_cap.max_jetty = 1024;
    mock_device_attr.dev_cap.max_jfs_sge = 8;
    mock_device_attr.dev_cap.max_jfr_sge = 8;
    memcpy(attr, &mock_device_attr, sizeof(urma_device_attr_t));
    return URMA_SUCCESS;
}

urma_eid_info_t *urma_get_eid_list(urma_device_t *device, uint32_t *eid_cnt) {
    if (!device || !eid_cnt) {
        return nullptr;
    }
    *eid_cnt = 1;
    auto *eid_list = new urma_eid_info_t[1];
    memcpy(eid_list, &mock_eid_info, sizeof(urma_eid_info_t));
    return eid_list;
}

void urma_free_eid_list(urma_eid_info_t *eid_list) {
    if (eid_list) {
        delete[] eid_list;
    }
}

urma_context_t *urma_create_context(urma_device_t *device, uint32_t eid_index) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!device) {
        return nullptr;
    }
    urma_context_t *ctx = new urma_context_t;
    ctx->async_fd = 0;
    ctx->dev = device;
    context_map[ctx] = 1;
    return ctx;
}

urma_status_t urma_delete_context(urma_context_t *ctx) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || context_map.find(ctx) == context_map.end()) {
        return URMA_EINVAL;
    }
    context_map.erase(ctx);
    delete ctx;
    return URMA_SUCCESS;
}

urma_jfce_t *urma_create_jfce(urma_context_t *ctx) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    // Allocate a real nonblocking eventfd so brpc's event-mode CQ socket can
    // be exercised by the mock as well.
    urma_jfce_t *jfce = new urma_jfce_t{};
    jfce->urma_ctx = ctx;
    jfce->fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (jfce->fd < 0) {
        delete jfce;
        return nullptr;
    }
    jfce_map[jfce] = 1;
    return jfce;
}

urma_status_t urma_delete_jfce(urma_jfce_t *jfce) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jfce || jfce_map.find(jfce) == jfce_map.end()) {
        return URMA_EINVAL;
    }
    jfce_map.erase(jfce);
    close(jfce->fd);
    delete jfce;
    return URMA_SUCCESS;
}

urma_jfc_t *urma_create_jfc(urma_context_t *ctx, urma_jfc_cfg_t *cfg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !cfg || context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    urma_jfc_t *jfc = new urma_jfc_t;
    memset(&jfc->jfc_id.eid, 0, sizeof(urma_eid_t));
    jfc->jfc_id.eid.raw[0] = 1;
    jfc->jfc_id.uasid = 0;
    jfc->jfc_id.id = 1;
    jfc->handle = cfg->user_ctx;
    jfc->comp_events_acked = 0;
    jfc->async_events_acked = 0;
    jfc->jfc_cfg = *cfg;
    jfc_state_map[jfc] = new JfcState();
    return jfc;
}

urma_status_t urma_delete_jfc(urma_jfc_t *jfc) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jfc || jfc_state_map.find(jfc) == jfc_state_map.end()) {
        return URMA_EINVAL;
    }
    delete jfc_state_map[jfc];
    jfc_state_map.erase(jfc);
    delete jfc;
    return URMA_SUCCESS;
}

urma_jfr_t *urma_create_jfr(urma_context_t *ctx, urma_jfr_cfg_t *cfg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !cfg || context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    // Opaque handle (avoids partially initializing urma_jfr_t's pthread
    // members). brpc's endpoint reads back jfr_cfg.jfc from the stored cfg
    // pointer below, so we keep a side-table mapping jfr -> cfg.jfc.
    urma_jfr_t *jfr = reinterpret_cast<urma_jfr_t *>(new int(1));
    jfr_map[jfr] = 1;
    // Stash the JFC for urma_post_jfr_wr's completion routing.
    jfr_jfc_map[jfr] = cfg->jfc;
    jfr_recv_map[jfr] = {};
    return jfr;
}

urma_status_t urma_delete_jfr(urma_jfr_t *jfr) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jfr || jfr_map.find(jfr) == jfr_map.end()) {
        return URMA_EINVAL;
    }
    jfr_map.erase(jfr);
    jfr_jfc_map.erase(jfr);
    jfr_recv_map.erase(jfr);
    delete reinterpret_cast<int *>(jfr);
    return URMA_SUCCESS;
}

urma_target_seg_t *urma_register_seg(urma_context_t *ctx, urma_seg_cfg_t *cfg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !cfg || context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    urma_target_seg_t *seg = new urma_target_seg_t;
    memset(&seg->seg.ubva.eid, 0, sizeof(urma_eid_t));
    seg->seg.ubva.eid.raw[0] = 1;
    seg->seg.ubva.uasid = 0;
    seg->seg.ubva.va = cfg->va;
    seg->seg.len = cfg->len;
    seg->seg.token_id = cfg->token_value.token;
    seg_map[seg] = 1;
    return seg;
}

urma_status_t urma_unregister_seg(urma_target_seg_t *seg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!seg || seg_map.find(seg) == seg_map.end()) {
        return URMA_EINVAL;
    }
    seg_map.erase(seg);
    delete seg;
    return URMA_SUCCESS;
}

urma_target_seg_t *urma_import_seg(urma_context_t *ctx, urma_seg_t *seg,
                                   urma_token_t *token_value, uint64_t addr,
                                   urma_import_seg_flag_t flag) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !seg || !token_value ||
        context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    urma_target_seg_t *tseg = new urma_target_seg_t;
    tseg->seg = *seg;
    *token_value = {.token = seg->token_id};
    seg_map[tseg] = 1;
    return tseg;
}

urma_status_t urma_unimport_seg(urma_target_seg_t *tseg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!tseg || seg_map.find(tseg) == seg_map.end()) {
        return URMA_EINVAL;
    }
    seg_map.erase(tseg);
    delete tseg;
    return URMA_SUCCESS;
}

urma_status_t urma_get_async_event(urma_context_t *ctx,
                                   urma_async_event_t *event) {
    if (!ctx || !event) {
        return URMA_EINVAL;
    }
    std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
    if (context_map.find(ctx) == context_map.end()) {
        return URMA_EINVAL;
    }
    return URMA_ETIMEOUT;
}

void urma_ack_async_event(urma_async_event_t *event) {}

urma_jetty_t *urma_create_jetty(urma_context_t *ctx, urma_jetty_cfg_t *cfg) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !cfg || context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    urma_jetty_t *jetty = new urma_jetty_t;
    memset(&jetty->jetty_id.eid, 0, sizeof(urma_eid_t));
    jetty->jetty_id.eid.raw[0] = 1;
    jetty->jetty_id.uasid = 0;
    jetty->jetty_id.id = next_jetty_id.fetch_add(1);
    jetty->jetty_cfg = *cfg;
    jetty->remote_jetty = nullptr;
    jetty_map[jetty] = 1;
    jetty_id_map[jetty->jetty_id.id] = jetty;
    return jetty;
}

urma_status_t urma_delete_jetty(urma_jetty_t *jetty) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jetty || jetty_map.find(jetty) == jetty_map.end()) {
        return URMA_EINVAL;
    }
    jetty_id_map.erase(jetty->jetty_id.id);
    jetty_map.erase(jetty);
    delete jetty;
    return URMA_SUCCESS;
}

urma_status_t urma_unbind_jetty(urma_jetty_t *jetty) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jetty || jetty_map.find(jetty) == jetty_map.end()) {
        return URMA_EINVAL;
    }
    jetty->remote_jetty = nullptr;
    return URMA_SUCCESS;
}

urma_target_jetty_t *urma_import_jetty(urma_context_t *ctx,
                                       urma_rjetty_t *rjetty,
                                       urma_token_t *token_value) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!ctx || !rjetty || !token_value ||
        context_map.find(ctx) == context_map.end()) {
        return nullptr;
    }
    urma_target_jetty_t *tjetty = new urma_target_jetty_t;
    tjetty->id = rjetty->jetty_id;
    target_jetty_map[tjetty] = 1;
    *token_value = {.token = 1};
    return tjetty;
}

urma_status_t urma_unimport_jetty(urma_target_jetty_t *tjetty) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!tjetty || target_jetty_map.find(tjetty) == target_jetty_map.end()) {
        return URMA_EINVAL;
    }
    target_jetty_map.erase(tjetty);
    delete tjetty;
    return URMA_SUCCESS;
}

urma_status_t urma_bind_jetty(urma_jetty_t *jetty,
                              urma_target_jetty_t *tjetty) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jetty || !tjetty || jetty_map.find(jetty) == jetty_map.end() ||
        target_jetty_map.find(tjetty) == target_jetty_map.end()) {
        return URMA_EINVAL;
    }
    jetty->remote_jetty = tjetty;
    return URMA_SUCCESS;
}

urma_status_t urma_modify_jetty(urma_jetty_t *jetty, urma_jetty_attr_t *attr) {
    std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
    if (!jetty || !attr || jetty_map.find(jetty) == jetty_map.end()) {
        return URMA_EINVAL;
    }
    return URMA_SUCCESS;
}

urma_status_t urma_post_jetty_send_wr(urma_jetty_t *jetty, urma_jfs_wr_t *wr,
                                      urma_jfs_wr_t **bad_wr) {
    std::shared_lock<std::shared_mutex> read_lock(g_rw_mutex);
    auto local_it = jetty_map.find(jetty);
    auto local_jfc_it =
        jetty ? jfc_state_map.find(jetty->jetty_cfg.jfs_cfg.jfc)
              : jfc_state_map.end();
    if (!jetty || !wr || local_it == jetty_map.end() ||
        local_jfc_it == jfc_state_map.end()) {
        if (bad_wr) {
            *bad_wr = wr;
        }
        return URMA_EINVAL;
    }
    JfcState* local_state = local_jfc_it->second;
    read_lock.unlock();

    for (urma_jfs_wr_t* current = wr; current; current = current->next) {
        if (!current->tjetty) {
            if (bad_wr) {
                *bad_wr = current;
            }
            return URMA_EINVAL;
        }

        urma_cr_t send_cr{};
        send_cr.status = URMA_CR_SUCCESS;
        send_cr.user_ctx = current->user_ctx;
        send_cr.flag.bs.s_r = 0;
        if (current->flag.bs.complete_enable) {
            PushCompletion(jetty->jetty_cfg.jfs_cfg.jfc,
                           local_state, send_cr);
        }

        PendingRecv recv{};
        urma_jfc_t* remote_jfc = nullptr;
        {
            std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
            auto remote_it = jetty_id_map.find(current->tjetty->id.id);
            if (remote_it == jetty_id_map.end()) {
                continue;
            }
            urma_jfr_t* remote_jfr =
                remote_it->second->jetty_cfg.shared.jfr;
            auto recv_it = jfr_recv_map.find(remote_jfr);
            auto jfc_it = jfr_jfc_map.find(remote_jfr);
            if (recv_it == jfr_recv_map.end() || recv_it->second.empty() ||
                jfc_it == jfr_jfc_map.end()) {
                continue;
            }
            recv = recv_it->second.front();
            recv_it->second.pop_front();
            remote_jfc = jfc_it->second;
        }

        uint32_t copied = 0;
        for (uint32_t i = 0;
             i < current->send.src.num_sge && copied < recv.len; ++i) {
            const urma_sge_t& sge = current->send.src.sge[i];
            const uint32_t n = std::min(sge.len, recv.len - copied);
            std::memcpy(reinterpret_cast<void*>(recv.addr + copied),
                        reinterpret_cast<const void*>(sge.addr), n);
            copied += n;
        }

        JfcState* remote_state = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
            auto state_it = jfc_state_map.find(remote_jfc);
            if (state_it != jfc_state_map.end()) {
                remote_state = state_it->second;
            }
        }
        if (remote_state) {
            urma_cr_t recv_cr{};
            recv_cr.status = URMA_CR_SUCCESS;
            recv_cr.user_ctx = recv.user_ctx;
            recv_cr.flag.bs.s_r = 1;
            recv_cr.completion_len = copied;
            recv_cr.opcode =
                current->opcode == URMA_OPC_SEND_IMM
                    ? URMA_CR_OPC_SEND_WITH_IMM
                    : URMA_CR_OPC_SEND;
            recv_cr.imm_data = current->send.imm_data;
            PushCompletion(remote_jfc, remote_state, recv_cr);
        }
    }
    if (bad_wr) {
        *bad_wr = nullptr;
    }
    return URMA_SUCCESS;
}

urma_status_t urma_post_jfr_wr(urma_jfr_t *jfr, urma_jfr_wr_t *wr,
                               urma_jfr_wr_t **bad_wr) {
    std::unique_lock<std::shared_mutex> lock(g_rw_mutex);
    auto recv_it = jfr_recv_map.find(jfr);
    if (!jfr || !wr || recv_it == jfr_recv_map.end()) {
        if (bad_wr) {
            *bad_wr = wr;
        }
        return URMA_EINVAL;
    }
    for (urma_jfr_wr_t* current = wr; current; current = current->next) {
        if (current->src.num_sge == 0 || !current->src.sge) {
            if (bad_wr) {
                *bad_wr = current;
            }
            return URMA_EINVAL;
        }
        recv_it->second.push_back(PendingRecv{
            current->src.sge[0].addr,
            current->src.sge[0].len,
            current->user_ctx});
    }
    if (bad_wr) {
        *bad_wr = nullptr;
    }
    return URMA_SUCCESS;
}

urma_status_t urma_post_jetty_recv_wr(urma_jetty_t *jetty,
                                      urma_jfr_wr_t *wr,
                                      urma_jfr_wr_t **bad_wr) {
    urma_jfr_t* shared_jfr = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
        if (!jetty || jetty_map.find(jetty) == jetty_map.end()) {
            if (bad_wr) {
                *bad_wr = wr;
            }
            return URMA_EINVAL;
        }
        shared_jfr = jetty->jetty_cfg.shared.jfr;
    }
    return urma_post_jfr_wr(shared_jfr, wr, bad_wr);
}

int urma_poll_jfc(urma_jfc_t *jfc, int num_entries, urma_cr_t *cr_list) {
    JfcState* state = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
        auto it = jfc_state_map.find(jfc);
        if (it == jfc_state_map.end()) {
            return -1;
        }
        state = it->second;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    int count = 0;
    while (count < num_entries && !state->completions.empty()) {
        cr_list[count++] = state->completions.front();
        state->completions.pop_front();
    }
    if (state->completions.empty()) {
        state->event_pending = false;
    }
    return count;
}

urma_status_t urma_rearm_jfc(urma_jfc_t*, bool) {
    return URMA_SUCCESS;
}

int urma_wait_jfc(urma_jfce_t* jfce, uint32_t jfc_cnt, int,
                  urma_jfc_t* jfcs[]) {
    if (!jfce || !jfcs || jfc_cnt == 0) {
        errno = EINVAL;
        return -1;
    }
    uint64_t value = 0;
    (void)read(jfce->fd, &value, sizeof(value));
    std::shared_lock<std::shared_mutex> lock(g_rw_mutex);
    uint32_t count = 0;
    for (const auto& item : jfc_state_map) {
        if (count >= jfc_cnt || item.first->jfc_cfg.jfce != jfce) {
            continue;
        }
        std::lock_guard<std::mutex> state_lock(item.second->mutex);
        if (item.second->event_pending) {
            jfcs[count++] = item.first;
            item.second->event_pending = false;
        }
    }
    return static_cast<int>(count);
}

void urma_ack_jfc(urma_jfc_t*[], uint32_t[], uint32_t) {
}

}  // extern "C"

#endif  // BRPC_WITH_URMA
