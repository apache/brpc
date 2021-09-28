// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/posix/global_descriptors.h"

#include <vector>
#include <utility>

#include "butil/logging.h"

namespace butil {

// static
GlobalDescriptors* GlobalDescriptors::GetInstance() {
  typedef Singleton<butil::GlobalDescriptors,
                    LeakySingletonTraits<butil::GlobalDescriptors> >
      GlobalDescriptorsSingleton;
  return GlobalDescriptorsSingleton::get();
}

int GlobalDescriptors::Get(Key key) const {
  const int ret = MaybeGet(key);

  if (ret == -1)
    DLOG(FATAL) << "Unknown global descriptor: " << key;
  return ret;
}

int GlobalDescriptors::MaybeGet(Key key) const {
  for (Mapping::const_iterator
       i = descriptors_.begin(); i != descriptors_.end(); ++i) {
    if (i->first == key)
      return i->second;
  }

  return -1;
}

void GlobalDescriptors::Set(Key key, int fd) {
  for (Mapping::iterator
       i = descriptors_.begin(); i != descriptors_.end(); ++i) {
    if (i->first == key) {
      i->second = fd;
      return;
    }
  }

  descriptors_.emplace_back(key, fd);
}

void GlobalDescriptors::Reset(const Mapping& mapping) {
  descriptors_ = mapping;
}

GlobalDescriptors::GlobalDescriptors() {}

GlobalDescriptors::~GlobalDescriptors() {}

}  // namespace butil
