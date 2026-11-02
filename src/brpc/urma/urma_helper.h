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

#ifndef BRPC_URMA_HELPER_H
#define BRPC_URMA_HELPER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "bthread/types.h"
#include "butil/atomicops.h"

namespace butil {
class IOBuf;
}

#if BRPC_WITH_URMA

#include "urma_api.h"
#include "brpc/urma/urma_qos.h"
#include "urma_types.h"

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);
namespace urma {

// Initialize the URMA environment. Failure disables URMA globally so
// individual sockets transparently fall back to TCP.
void GlobalUrmaInitializeOrDie();

// Initialize URMA polling mode for a given bthread tag.
// Returns false on failure.
bool InitPollingModeWithTag(bthread_tag_t tag,
                            std::function<void()> callback = nullptr,
                            std::function<void()> init_fn = nullptr,
                            std::function<void()> release_fn = nullptr);

void ReleasePollingModeWithTag(bthread_tag_t tag);

// Register the given user buffer for URMA access.
// Returns the (opaque, non-zero) target segment handle stored in the user-mr
// table; 0 on failure. To use the memory in an IOBuf, append it via
// append_user_data_with_meta and pass the returned handle as the data meta.
uint64_t RegisterMemoryForUrma(void* buf, size_t len);

// Deregister a previously registered user buffer.
void DeregisterMemoryForUrma(void* buf);

// Return the target segment for a buffer-pool address. Passing nullptr returns
// the segment backing the whole pool. Returns nullptr for any other address.
urma_target_seg_t* GetPoolSegFor(void* buf);

// Get the global URMA context (the urma_context_t created on the selected
// device / EID). Returns nullptr if URMA is not initialized.
urma_context_t* GetUrmaContext();

// Get the EID selected when the global context was created. This is the
// device EID that must be advertised to peers, especially for bonding
// devices where a created jetty may expose a provider-specific physical EID.
// Returns nullptr if URMA is not initialized.
const urma_eid_t* GetUrmaLocalEid();

// Return true when the selected URMA device is a bonding provider device.
bool IsUrmaBondingDevice();

// Find the first priority advertising @tp_type, including combined capabilities.
// Returns -1 when the device does not report one.
int FindUrmaPriorityForTpType(const urma_device_attr_t& attr,
                              urma_tp_type_t tp_type);

// Select an RM path from "auto", "ctp", or "rtp". Auto prefers CTP, then RTP.
// Providers without a capability table retain CTP/6 or explicit RTP/0.
bool SelectUrmaTransportPath(const urma_device_attr_t& attr,
                             const std::string& requested,
                             urma_tp_type_t* tp_type, uint8_t* priority);

// Return the process-wide path and priority selected at initialization.
urma_tp_type_t GetUrmaTpType();
uint8_t GetUrmaJettyPriority();

// Check whether a priority advertises the selected RM path.
bool IsUrmaPrioritySupported(const urma_device_attr_t& attr, int priority,
                             urma_tp_type_t tp_type);
// Resolve a requested priority/SL pair against a device capability snapshot.
// Fully automatic options retain default_priority, including legacy fallback.
bool ResolveUrmaJettyPriority(const urma_device_attr_t& attr,
                              const UrmaQosOptions& qos, urma_tp_type_t tp_type,
                              uint8_t default_priority, uint8_t* priority);
// Resolve automatic selection or validate against the initialized device.
bool ResolveUrmaJettyPriority(const UrmaQosOptions& qos, uint8_t* priority);
// Return the advertised SL on the selected path, or -1 when no initialized mapping is available.
int64_t GetUrmaServiceLevelForPriority(int priority);

// Initialize a local SGE whose complete byte range belongs to this context.
bool InitUrmaLocalSge(urma_sge_t* sge, uint64_t address, size_t length,
                      urma_target_seg_t* segment);

constexpr size_t URMA_SEND_BUFFER_ALIGNMENT = 64;
// Copy host payloads into one aligned block and transfer its ownership to buffer.
bool CopyUrmaSendBuffer(butil::IOBuf* buffer, urma_sge_t* sge,
                        urma_target_seg_t* (*lookup)(void*) = GetPoolSegFor);

// If the URMA environment is available.
bool IsUrmaAvailable();

// Disable URMA for the remaining lifetime of the process.
void GlobalDisableUrma();

// If the given protocol is supported by UrmaTransport.
// Currently only "baidu_std" is supported.
bool SupportedByUrma(const std::string& protocol);

// Return the configured recv buffer size (one URMA recv WR's payload size).
size_t GetUrmaRecvBlockSize();

// Return max_sge supported by the device.
int GetUrmaMaxSge();

}  // namespace urma
}  // namespace brpc

#else  // BRPC_WITH_URMA

namespace brpc {
namespace urma {

// Initialize the URMA environment.
// Exit the process if initialization fails.
void GlobalUrmaInitializeOrDie();

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA

#endif  // BRPC_URMA_HELPER_H
