// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_DEBUG_ADDRESS_ANNOTATIONS_H_
#define BUTIL_DEBUG_ADDRESS_ANNOTATIONS_H_

#include "butil/macros.h"

#ifdef BUTIL_USE_ASAN

// Public ASan API from <sanitizer/asan_interface.h>.
extern "C" {
void __asan_poison_memory_region(void const volatile*, size_t);
void __asan_unpoison_memory_region(void const volatile*, size_t);
void __sanitizer_start_switch_fiber(void**, const void*, size_t);
void __sanitizer_finish_switch_fiber(void*, const void**, size_t*);
int __asan_address_is_poisoned(void const volatile *addr);
} // extern "C"

#define BUTIL_ASAN_POISON_MEMORY_REGION(addr, size) \
    __asan_poison_memory_region(addr, size)

#define BUTIL_ASAN_UNPOISON_MEMORY_REGION(addr, size) \
    __asan_unpoison_memory_region(addr, size)

#define BUTIL_ASAN_ADDRESS_IS_POISONED(addr) \
    __asan_address_is_poisoned(addr)

#define BUTIL_ASAN_START_SWITCH_FIBER(fake_stack_save, bottom, size) \
    __sanitizer_start_switch_fiber(fake_stack_save, bottom, size)

#define BUTIL_ASAN_FINISH_SWITCH_FIBER(fake_stack_save, bottom_old, size_old) \
    __sanitizer_finish_switch_fiber(fake_stack_save, bottom_old, size_old)

#else
// If ASan are used, the annotations should be no-ops.
#define BUTIL_ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define BUTIL_ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define BUTIL_ASAN_START_SWITCH_FIBER(fake_stack_save, bottom, size) \
    ((void)(fake_stack_save), (void)(bottom), (void)(size))
#define BUTIL_ASAN_FINISH_SWITCH_FIBER(fake_stack_save, bottom_old, size_old) \
    ((void)(fake_stack_save), (void)(bottom_old), (void)(size_old))
#endif // BUTIL_USE_ASAN

#endif  // BUTIL_DEBUG_ADDRESS_ANNOTATIONS_H_
