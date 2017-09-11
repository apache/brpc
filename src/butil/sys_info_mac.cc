// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/sys_info.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include "butil/logging.h"
#include "butil/mac/scoped_mach_port.h"
#include "butil/strings/stringprintf.h"

namespace butil {

// static
std::string SysInfo::OperatingSystemName() {
  return "Mac OS X";
}

// static
std::string SysInfo::OperatingSystemVersion() {
  int32_t major, minor, bugfix;
  OperatingSystemVersionNumbers(&major, &minor, &bugfix);
  return butil::StringPrintf("%d.%d.%d", major, minor, bugfix);
}

// static
void SysInfo::OperatingSystemVersionNumbers(int32_t* major_version,
                                            int32_t* minor_version,
                                            int32_t* bugfix_version) {
  Gestalt(gestaltSystemVersionMajor,
      reinterpret_cast<SInt32*>(major_version));
  Gestalt(gestaltSystemVersionMinor,
      reinterpret_cast<SInt32*>(minor_version));
  Gestalt(gestaltSystemVersionBugFix,
      reinterpret_cast<SInt32*>(bugfix_version));
}

// static
int64_t SysInfo::AmountOfPhysicalMemory() {
  struct host_basic_info hostinfo;
  mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
  butil::mac::ScopedMachSendRight host(mach_host_self());
  int result = host_info(host,
                         HOST_BASIC_INFO,
                         reinterpret_cast<host_info_t>(&hostinfo),
                         &count);
  if (result != KERN_SUCCESS) {
    NOTREACHED();
    return 0;
  }
  DCHECK_EQ(HOST_BASIC_INFO_COUNT, count);
  return static_cast<int64_t>(hostinfo.max_mem);
}

// static
int64_t SysInfo::AmountOfAvailablePhysicalMemory() {
  butil::mac::ScopedMachSendRight host(mach_host_self());
  vm_statistics_data_t vm_info;
  mach_msg_type_number_t count = HOST_VM_INFO_COUNT;

  if (host_statistics(host.get(),
                      HOST_VM_INFO,
                      reinterpret_cast<host_info_t>(&vm_info),
                      &count) != KERN_SUCCESS) {
    NOTREACHED();
    return 0;
  }

  return static_cast<int64_t>(
      vm_info.free_count - vm_info.speculative_count) * PAGE_SIZE;
}

// static
std::string SysInfo::CPUModelName() {
  char name[256];
  size_t len = arraysize(name);
  if (sysctlbyname("machdep.cpu.brand_string", &name, &len, NULL, 0) == 0)
    return name;
  return std::string();
}

}  // namespace butil
