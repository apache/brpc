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

#ifndef BRPC_RDMA_HELPER_H
#define BRPC_RDMA_HELPER_H

#if BRPC_WITH_RDMA

#include <infiniband/verbs.h>
#include <string>


namespace brpc {
namespace rdma {

// Initialize RDMA environment
// Exit if failed
void GlobalRdmaInitializeOrDie();

// Register the given memory
// Return the memory lkey for the given memory, Return 0 when fails
// To use the memory in IOBuf, append_user_data_with_meta must be called
// and take the lkey as the data meta
uint32_t RegisterMemoryForRdma(void* buf, size_t len);

// Deregister the given memory
void DeregisterMemoryForRdma(void* buf);

// Get global RDMA context
ibv_context* GetRdmaContext();

// Get global RDMA protection domain
ibv_pd* GetRdmaPd();

// Return lkey of the given address
uint32_t GetLKey(void* buf);

// Return GID Index
uint8_t GetRdmaGidIndex();

// Return Global GID
ibv_gid GetRdmaGid();

// Return Global LID
uint16_t GetRdmaLid();

// Return suggested comp vector for CQ
int GetRdmaCompVector();

// Return current port number used
uint8_t GetRdmaPortNum();

// Get max_sge supported by the device
int GetRdmaMaxSge();

// Get suggested comp_vector for a new CQ
int GetCompVector();

// If the RDMA environment is available
bool IsRdmaAvailable();

// Disable RDMA in the remaining lifetime of the process
void GlobalDisableRdma();

// If the given protocol supported by RDMA
bool SupportedByRdma(std::string protocol);

}  // namespace rdma
}  // namespace brpc

#endif  // if BRPC_WITH_RDMA

#endif  // BRPC_RDMA_HELPER_H
