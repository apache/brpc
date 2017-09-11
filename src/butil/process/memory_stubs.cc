// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/process/memory.h"

namespace butil {

void EnableTerminationOnOutOfMemory() {
}

void EnableTerminationOnHeapCorruption() {
}

bool AdjustOOMScore(ProcessId process, int score) {
  return false;
}

}  // namespace butil
