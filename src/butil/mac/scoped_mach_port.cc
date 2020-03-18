// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "butil/mac/scoped_mach_port.h"
#include "butil/logging.h"

namespace butil {
namespace mac {
namespace internal {

// static
void SendRightTraits::Free(mach_port_t port) {
  kern_return_t kr = mach_port_deallocate(mach_task_self(), port);
  LOG_IF(ERROR, kr != KERN_SUCCESS) << "Fail to call mach_port_deallocate";
}

// static
void ReceiveRightTraits::Free(mach_port_t port) {
  kern_return_t kr =
      mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
  LOG_IF(ERROR, kr != KERN_SUCCESS) << "Fail to call mach_port_mod_refs";
}

// static
void PortSetTraits::Free(mach_port_t port) {
  kern_return_t kr =
      mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_PORT_SET, -1);
  LOG_IF(ERROR, kr != KERN_SUCCESS) << "Fail to call mach_port_mod_refs";
}

}  // namespace internal
}  // namespace mac
}  // namespace butil
