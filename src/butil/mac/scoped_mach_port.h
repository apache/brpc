// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_SCOPED_MACH_PORT_H_
#define BASE_MAC_SCOPED_MACH_PORT_H_

#include <mach/mach.h>

#include "butil/base_export.h"
#include "butil/scoped_generic.h"

namespace butil {
namespace mac {

namespace internal {

struct SendRightTraits {
  static mach_port_t InvalidValue() {
    return MACH_PORT_NULL;
  }

  static void Free(mach_port_t port);
};

struct ReceiveRightTraits {
  static mach_port_t InvalidValue() {
    return MACH_PORT_NULL;
  }

  static void Free(mach_port_t port);
};

struct PortSetTraits {
  static mach_port_t InvalidValue() {
    return MACH_PORT_NULL;
  }

  static void Free(mach_port_t port);
};

}  // namespace internal

// A scoper for handling a Mach port that names a send right. Send rights are
// reference counted, and this takes ownership of the right on construction
// and then removes a reference to the right on destruction. If the reference
// is the last one on the right, the right is deallocated.
using ScopedMachSendRight =
    ScopedGeneric<mach_port_t, internal::SendRightTraits>;

// A scoper for handling a Mach port's receive right. There is only one
// receive right per port. This takes ownership of the receive right on
// construction and then destroys the right on destruction, turning all
// outstanding send rights into dead names.
using ScopedMachReceiveRight =
    ScopedGeneric<mach_port_t, internal::ReceiveRightTraits>;

// A scoper for handling a Mach port set. A port set can have only one
// reference. This takes ownership of that single reference on construction and
// destroys the port set on destruction. Destroying a port set does not destroy
// the receive rights that are members of the port set.
using ScopedMachPortSet = ScopedGeneric<mach_port_t, internal::PortSetTraits>;

}  // namespace mac
}  // namespace butil

#endif  // BASE_MAC_SCOPED_MACH_PORT_H_
