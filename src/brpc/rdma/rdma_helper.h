// Copyright (c) 2014 baidu-rpc authors.
// 
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

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMA_HELPER_H
#define BRPC_RDMA_HELPER_H

#include <netinet/in.h>

namespace brpc {
namespace rdma {

// Initialize RDMA environment
// Exit if failed
void GlobalRdmaInitializeOrDie();

// If the given address is in the same RDMA cluster
bool DestinationInRdmaCluster(in_addr_t addr);

// Register the given memory
// Return 0 if success, -1 if failed and errno set
int RegisterMemoryForRdma(void* buf, size_t len);

// Deregister the given memory
void DeregisterMemoryForRdma(void* buf);

// Get global RDMA context
void* GetRdmaContext();

// Get global RDMA protection domain
void* GetRdmaProtectionDomain();

// Get global RDMA IP
in_addr GetRdmaIP();

// Whether the give addr is the local address
bool IsLocalIP(in_addr addr);

// Return lkey of the given address
uint32_t GetLKey(const void* buf);

// Get max_sge supported by the device
int GetRdmaMaxSge();

// If the RDMA environment is available
bool IsRdmaAvailable();

// Disable RDMA in the remaining lifetime of the process
void GlobalDisableRdma();

// If the given protocol supported by RDMA
bool SupportedByRdma(std::string protocol);

}  // namespace rdma
}  // namespace brpc

#endif // BRPC_RDMA_HELPER_H
