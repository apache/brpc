// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/sys_info.h"

#include <limits>

#include "butil/file_util.h"
#include "butil/lazy_instance.h"
#include "butil/logging.h"
#include "butil/strings/string_number_conversions.h"
#include "butil/sys_info_internal.h"

namespace {

int64_t AmountOfMemory(int pages_name) {
  long pages = sysconf(pages_name);
  long page_size = sysconf(_SC_PAGESIZE);
  if (pages == -1 || page_size == -1) {
    NOTREACHED();
    return 0;
  }
  return static_cast<int64_t>(pages) * page_size;
}

int64_t AmountOfPhysicalMemory() {
  return AmountOfMemory(_SC_PHYS_PAGES);
}

size_t MaxSharedMemorySize() {
  std::string contents;
  butil::ReadFileToString(butil::FilePath("/proc/sys/kernel/shmmax"), &contents);
  DCHECK(!contents.empty());
  if (!contents.empty() && contents[contents.length() - 1] == '\n') {
    contents.erase(contents.length() - 1);
  }

  int64_t limit;
  if (!butil::StringToInt64(contents, &limit)) {
    limit = 0;
  }
  if (limit < 0 ||
      static_cast<uint64_t>(limit) > std::numeric_limits<size_t>::max()) {
    limit = 0;
  }
  DCHECK(limit > 0);
  return static_cast<size_t>(limit);
}

butil::LazyInstance<
    butil::internal::LazySysInfoValue<int64_t, AmountOfPhysicalMemory> >::Leaky
    g_lazy_physical_memory = LAZY_INSTANCE_INITIALIZER;
butil::LazyInstance<
    butil::internal::LazySysInfoValue<size_t, MaxSharedMemorySize> >::Leaky
    g_lazy_max_shared_memory = LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace butil {

// static
int64_t SysInfo::AmountOfAvailablePhysicalMemory() {
  return AmountOfMemory(_SC_AVPHYS_PAGES);
}

// static
int64_t SysInfo::AmountOfPhysicalMemory() {
  return g_lazy_physical_memory.Get().value();
}

// static
size_t SysInfo::MaxSharedMemorySize() {
  return g_lazy_max_shared_memory.Get().value();
}

// static
std::string SysInfo::CPUModelName() {
#if defined(OS_CHROMEOS) && defined(ARCH_CPU_ARMEL)
  const char kCpuModelPrefix[] = "Hardware";
#else
  const char kCpuModelPrefix[] = "model name";
#endif
  std::string contents;
  ReadFileToString(FilePath("/proc/cpuinfo"), &contents);
  DCHECK(!contents.empty());
  if (!contents.empty()) {
    std::istringstream iss(contents);
    std::string line;
    while (std::getline(iss, line)) {
      if (line.compare(0, strlen(kCpuModelPrefix), kCpuModelPrefix) == 0) {
        size_t pos = line.find(": ");
        return line.substr(pos + 2);
      }
    }
  }
  return std::string();
}

}  // namespace butil
