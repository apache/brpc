// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_DEBUG_LEAK_ANNOTATIONS_H_
#define BUTIL_DEBUG_LEAK_ANNOTATIONS_H_

#include "butil/basictypes.h"
#include "butil/build_config.h"

// This file defines macros which can be used to annotate intentional memory
// leaks. Support for annotations is implemented in LeakSanitizer. Annotated
// objects will be treated as a source of live pointers, i.e. any heap objects
// reachable by following pointers from an annotated object will not be
// reported as leaks.
//
// ANNOTATE_SCOPED_MEMORY_LEAK: all allocations made in the current scope
// will be annotated as leaks.
// ANNOTATE_LEAKING_OBJECT_PTR(X): the heap object referenced by pointer X will
// be annotated as a leak.

#if (defined(LEAK_SANITIZER) && !defined(OS_NACL)) || defined(BUTIL_USE_ASAN)

// Public LSan API from <sanitizer/lsan_interface.h>.
extern "C" {
void __lsan_disable();
void __lsan_enable();
void __lsan_ignore_object(const void *p);

// Invoke leak detection immediately. If leaks are found, the process will exit.
void __lsan_do_leak_check();
}  // extern "C"

class ScopedLeakSanitizerDisabler {
 public:
  ScopedLeakSanitizerDisabler() { __lsan_disable(); }
  ~ScopedLeakSanitizerDisabler() { __lsan_enable(); }
 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedLeakSanitizerDisabler);
};

#define ANNOTATE_SCOPED_MEMORY_LEAK \
    ScopedLeakSanitizerDisabler leak_sanitizer_disabler; static_cast<void>(0)

#define ANNOTATE_LEAKING_OBJECT_PTR(X) __lsan_ignore_object(X);

#else

// If neither HeapChecker nor LSan are used, the annotations should be no-ops.
#define ANNOTATE_SCOPED_MEMORY_LEAK ((void)0)
#define ANNOTATE_LEAKING_OBJECT_PTR(X) ((void)0)

#endif

#endif  // BUTIL_DEBUG_LEAK_ANNOTATIONS_H_
