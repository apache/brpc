// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/memory.h"

#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#import <objc/runtime.h>

#include <new>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/mac/mac_util.h"
#include "base/mac/mach_logging.h"
#include "base/scoped_clear_errno.h"
#include "third_party/apple_apsl/CFBase.h"
#include "third_party/apple_apsl/malloc.h"

#if ARCH_CPU_32_BITS
#include <dlfcn.h>
#include <mach-o/nlist.h>

#include "base/threading/thread_local.h"
#include "third_party/mach_override/mach_override.h"
#endif  // ARCH_CPU_32_BITS

namespace base {

// These are helpers for EnableTerminationOnHeapCorruption, which is a no-op
// on 64 bit Macs.
#if ARCH_CPU_32_BITS
namespace {

// Finds the library path for malloc() and thus the libC part of libSystem,
// which in Lion is in a separate image.
const char* LookUpLibCPath() {
  const void* addr = reinterpret_cast<void*>(&malloc);

  Dl_info info;
  if (dladdr(addr, &info))
    return info.dli_fname;

  DLOG(WARNING) << "Could not find image path for malloc()";
  return NULL;
}

typedef void(*malloc_error_break_t)(void);
malloc_error_break_t g_original_malloc_error_break = NULL;

// Returns the function pointer for malloc_error_break. This symbol is declared
// as __private_extern__ and cannot be dlsym()ed. Instead, use nlist() to
// get it.
malloc_error_break_t LookUpMallocErrorBreak() {
  const char* lib_c_path = LookUpLibCPath();
  if (!lib_c_path)
    return NULL;

  // Only need to look up two symbols, but nlist() requires a NULL-terminated
  // array and takes no count.
  struct nlist nl[3];
  bzero(&nl, sizeof(nl));

  // The symbol to find.
  nl[0].n_un.n_name = const_cast<char*>("_malloc_error_break");

  // A reference symbol by which the address of the desired symbol will be
  // calculated.
  nl[1].n_un.n_name = const_cast<char*>("_malloc");

  int rv = nlist(lib_c_path, nl);
  if (rv != 0 || nl[0].n_type == N_UNDF || nl[1].n_type == N_UNDF) {
    return NULL;
  }

  // nlist() returns addresses as offsets in the image, not the instruction
  // pointer in memory. Use the known in-memory address of malloc()
  // to compute the offset for malloc_error_break().
  uintptr_t reference_addr = reinterpret_cast<uintptr_t>(&malloc);
  reference_addr -= nl[1].n_value;
  reference_addr += nl[0].n_value;

  return reinterpret_cast<malloc_error_break_t>(reference_addr);
}

// Combines ThreadLocalBoolean with AutoReset.  It would be convenient
// to compose ThreadLocalPointer<bool> with base::AutoReset<bool>, but that
// would require allocating some storage for the bool.
class ThreadLocalBooleanAutoReset {
 public:
  ThreadLocalBooleanAutoReset(ThreadLocalBoolean* tlb, bool new_value)
      : scoped_tlb_(tlb),
        original_value_(tlb->Get()) {
    scoped_tlb_->Set(new_value);
  }
  ~ThreadLocalBooleanAutoReset() {
    scoped_tlb_->Set(original_value_);
  }

 private:
  ThreadLocalBoolean* scoped_tlb_;
  bool original_value_;

  DISALLOW_COPY_AND_ASSIGN(ThreadLocalBooleanAutoReset);
};

base::LazyInstance<ThreadLocalBoolean>::Leaky
    g_unchecked_alloc = LAZY_INSTANCE_INITIALIZER;

// NOTE(shess): This is called when the malloc library noticed that the heap
// is fubar.  Avoid calls which will re-enter the malloc library.
void CrMallocErrorBreak() {
  g_original_malloc_error_break();

  // Out of memory is certainly not heap corruption, and not necessarily
  // something for which the process should be terminated. Leave that decision
  // to the OOM killer.
  if (errno == ENOMEM)
    return;

  // The malloc library attempts to log to ASL (syslog) before calling this
  // code, which fails accessing a Unix-domain socket when sandboxed.  The
  // failed socket results in writing to a -1 fd, leaving EBADF in errno.  If
  // UncheckedMalloc() is on the stack, for large allocations (15k and up) only
  // an OOM failure leads here.  Smaller allocations could also arrive here due
  // to freelist corruption, but there is no way to distinguish that from OOM at
  // this point.
  //
  // NOTE(shess): I hypothesize that EPERM case in 10.9 is the same root cause
  // as EBADF.  Unfortunately, 10.9's opensource releases don't include malloc
  // source code at this time.
  // <http://crbug.com/312234>
  if ((errno == EBADF || errno == EPERM) && g_unchecked_alloc.Get().Get())
    return;

  // A unit test checks this error message, so it needs to be in release builds.
  char buf[1024] =
      "Terminating process due to a potential for future heap corruption: "
      "errno=";
  char errnobuf[] = {
    '0' + ((errno / 100) % 10),
    '0' + ((errno / 10) % 10),
    '0' + (errno % 10),
    '\000'
  };
  COMPILE_ASSERT(ELAST <= 999, errno_too_large_to_encode);
  strlcat(buf, errnobuf, sizeof(buf));
  RAW_LOG(ERROR, buf);

  // Crash by writing to NULL+errno to allow analyzing errno from
  // crash dump info (setting a breakpad key would re-enter the malloc
  // library).  Max documented errno in intro(2) is actually 102, but
  // it really just needs to be "small" to stay on the right vm page.
  const int kMaxErrno = 256;
  char* volatile death_ptr = NULL;
  death_ptr += std::min(errno, kMaxErrno);
  *death_ptr = '!';
}

}  // namespace
#endif  // ARCH_CPU_32_BITS

void EnableTerminationOnHeapCorruption() {
#if defined(ADDRESS_SANITIZER) || ARCH_CPU_64_BITS
  // AddressSanitizer handles heap corruption, and on 64 bit Macs, the malloc
  // system automatically abort()s on heap corruption.
  return;
#else
  // Only override once, otherwise CrMallocErrorBreak() will recurse
  // to itself.
  if (g_original_malloc_error_break)
    return;

  malloc_error_break_t malloc_error_break = LookUpMallocErrorBreak();
  if (!malloc_error_break) {
    DLOG(WARNING) << "Could not find malloc_error_break";
    return;
  }

  mach_error_t err = mach_override_ptr(
     (void*)malloc_error_break,
     (void*)&CrMallocErrorBreak,
     (void**)&g_original_malloc_error_break);

  if (err != err_none)
    DLOG(WARNING) << "Could not override malloc_error_break; error = " << err;
#endif  // defined(ADDRESS_SANITIZER) || ARCH_CPU_64_BITS
}

// ------------------------------------------------------------------------

namespace {

bool g_oom_killer_enabled;

// Starting with Mac OS X 10.7, the zone allocators set up by the system are
// read-only, to prevent them from being overwritten in an attack. However,
// blindly unprotecting and reprotecting the zone allocators fails with
// GuardMalloc because GuardMalloc sets up its zone allocator using a block of
// memory in its bss. Explicit saving/restoring of the protection is required.
//
// This function takes a pointer to a malloc zone, de-protects it if necessary,
// and returns (in the out parameters) a region of memory (if any) to be
// re-protected when modifications are complete. This approach assumes that
// there is no contention for the protection of this memory.
void DeprotectMallocZone(ChromeMallocZone* default_zone,
                         mach_vm_address_t* reprotection_start,
                         mach_vm_size_t* reprotection_length,
                         vm_prot_t* reprotection_value) {
  mach_port_t unused;
  *reprotection_start = reinterpret_cast<mach_vm_address_t>(default_zone);
  struct vm_region_basic_info_64 info;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  kern_return_t result =
      mach_vm_region(mach_task_self(),
                     reprotection_start,
                     reprotection_length,
                     VM_REGION_BASIC_INFO_64,
                     reinterpret_cast<vm_region_info_t>(&info),
                     &count,
                     &unused);
  MACH_CHECK(result == KERN_SUCCESS, result) << "mach_vm_region";

  // The kernel always returns a null object for VM_REGION_BASIC_INFO_64, but
  // balance it with a deallocate in case this ever changes. See 10.9.2
  // xnu-2422.90.20/osfmk/vm/vm_map.c vm_map_region.
  mach_port_deallocate(mach_task_self(), unused);

  // Does the region fully enclose the zone pointers? Possibly unwarranted
  // simplification used: using the size of a full version 8 malloc zone rather
  // than the actual smaller size if the passed-in zone is not version 8.
  CHECK(*reprotection_start <=
            reinterpret_cast<mach_vm_address_t>(default_zone));
  mach_vm_size_t zone_offset = reinterpret_cast<mach_vm_size_t>(default_zone) -
      reinterpret_cast<mach_vm_size_t>(*reprotection_start);
  CHECK(zone_offset + sizeof(ChromeMallocZone) <= *reprotection_length);

  if (info.protection & VM_PROT_WRITE) {
    // No change needed; the zone is already writable.
    *reprotection_start = 0;
    *reprotection_length = 0;
    *reprotection_value = VM_PROT_NONE;
  } else {
    *reprotection_value = info.protection;
    result = mach_vm_protect(mach_task_self(),
                             *reprotection_start,
                             *reprotection_length,
                             false,
                             info.protection | VM_PROT_WRITE);
    MACH_CHECK(result == KERN_SUCCESS, result) << "mach_vm_protect";
  }
}

// === C malloc/calloc/valloc/realloc/posix_memalign ===

typedef void* (*malloc_type)(struct _malloc_zone_t* zone,
                             size_t size);
typedef void* (*calloc_type)(struct _malloc_zone_t* zone,
                             size_t num_items,
                             size_t size);
typedef void* (*valloc_type)(struct _malloc_zone_t* zone,
                             size_t size);
typedef void (*free_type)(struct _malloc_zone_t* zone,
                          void* ptr);
typedef void* (*realloc_type)(struct _malloc_zone_t* zone,
                              void* ptr,
                              size_t size);
typedef void* (*memalign_type)(struct _malloc_zone_t* zone,
                               size_t alignment,
                               size_t size);

malloc_type g_old_malloc;
calloc_type g_old_calloc;
valloc_type g_old_valloc;
free_type g_old_free;
realloc_type g_old_realloc;
memalign_type g_old_memalign;

malloc_type g_old_malloc_purgeable;
calloc_type g_old_calloc_purgeable;
valloc_type g_old_valloc_purgeable;
free_type g_old_free_purgeable;
realloc_type g_old_realloc_purgeable;
memalign_type g_old_memalign_purgeable;

void* oom_killer_malloc(struct _malloc_zone_t* zone,
                        size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_malloc(zone, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_calloc(struct _malloc_zone_t* zone,
                        size_t num_items,
                        size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_calloc(zone, num_items, size);
  if (!result && num_items && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_valloc(struct _malloc_zone_t* zone,
                        size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_valloc(zone, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void oom_killer_free(struct _malloc_zone_t* zone,
                     void* ptr) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  g_old_free(zone, ptr);
}

void* oom_killer_realloc(struct _malloc_zone_t* zone,
                         void* ptr,
                         size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_realloc(zone, ptr, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_memalign(struct _malloc_zone_t* zone,
                          size_t alignment,
                          size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_memalign(zone, alignment, size);
  // Only die if posix_memalign would have returned ENOMEM, since there are
  // other reasons why NULL might be returned (see
  // http://opensource.apple.com/source/Libc/Libc-583/gen/malloc.c ).
  if (!result && size && alignment >= sizeof(void*)
      && (alignment & (alignment - 1)) == 0) {
    debug::BreakDebugger();
  }
  return result;
}

void* oom_killer_malloc_purgeable(struct _malloc_zone_t* zone,
                                  size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_malloc_purgeable(zone, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_calloc_purgeable(struct _malloc_zone_t* zone,
                                  size_t num_items,
                                  size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_calloc_purgeable(zone, num_items, size);
  if (!result && num_items && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_valloc_purgeable(struct _malloc_zone_t* zone,
                                  size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_valloc_purgeable(zone, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void oom_killer_free_purgeable(struct _malloc_zone_t* zone,
                               void* ptr) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  g_old_free_purgeable(zone, ptr);
}

void* oom_killer_realloc_purgeable(struct _malloc_zone_t* zone,
                                   void* ptr,
                                   size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_realloc_purgeable(zone, ptr, size);
  if (!result && size)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_memalign_purgeable(struct _malloc_zone_t* zone,
                                    size_t alignment,
                                    size_t size) {
#if ARCH_CPU_32_BITS
  ScopedClearErrno clear_errno;
#endif  // ARCH_CPU_32_BITS
  void* result = g_old_memalign_purgeable(zone, alignment, size);
  // Only die if posix_memalign would have returned ENOMEM, since there are
  // other reasons why NULL might be returned (see
  // http://opensource.apple.com/source/Libc/Libc-583/gen/malloc.c ).
  if (!result && size && alignment >= sizeof(void*)
      && (alignment & (alignment - 1)) == 0) {
    debug::BreakDebugger();
  }
  return result;
}

// === C++ operator new ===

void oom_killer_new() {
  debug::BreakDebugger();
}

// === Core Foundation CFAllocators ===

bool CanGetContextForCFAllocator() {
  return !base::mac::IsOSLaterThanYosemite_DontCallThis();
}

CFAllocatorContext* ContextForCFAllocator(CFAllocatorRef allocator) {
  if (base::mac::IsOSSnowLeopard()) {
    ChromeCFAllocatorLeopards* our_allocator =
        const_cast<ChromeCFAllocatorLeopards*>(
            reinterpret_cast<const ChromeCFAllocatorLeopards*>(allocator));
    return &our_allocator->_context;
  } else if (base::mac::IsOSLion() ||
             base::mac::IsOSMountainLion() ||
             base::mac::IsOSMavericks() ||
             base::mac::IsOSYosemite()) {
    ChromeCFAllocatorLions* our_allocator =
        const_cast<ChromeCFAllocatorLions*>(
            reinterpret_cast<const ChromeCFAllocatorLions*>(allocator));
    return &our_allocator->_context;
  } else {
    return NULL;
  }
}

CFAllocatorAllocateCallBack g_old_cfallocator_system_default;
CFAllocatorAllocateCallBack g_old_cfallocator_malloc;
CFAllocatorAllocateCallBack g_old_cfallocator_malloc_zone;

void* oom_killer_cfallocator_system_default(CFIndex alloc_size,
                                            CFOptionFlags hint,
                                            void* info) {
  void* result = g_old_cfallocator_system_default(alloc_size, hint, info);
  if (!result)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_cfallocator_malloc(CFIndex alloc_size,
                                    CFOptionFlags hint,
                                    void* info) {
  void* result = g_old_cfallocator_malloc(alloc_size, hint, info);
  if (!result)
    debug::BreakDebugger();
  return result;
}

void* oom_killer_cfallocator_malloc_zone(CFIndex alloc_size,
                                         CFOptionFlags hint,
                                         void* info) {
  void* result = g_old_cfallocator_malloc_zone(alloc_size, hint, info);
  if (!result)
    debug::BreakDebugger();
  return result;
}

// === Cocoa NSObject allocation ===

typedef id (*allocWithZone_t)(id, SEL, NSZone*);
allocWithZone_t g_old_allocWithZone;

id oom_killer_allocWithZone(id self, SEL _cmd, NSZone* zone)
{
  id result = g_old_allocWithZone(self, _cmd, zone);
  if (!result)
    debug::BreakDebugger();
  return result;
}

}  // namespace

bool UncheckedMalloc(size_t size, void** result) {
  if (g_old_malloc) {
#if ARCH_CPU_32_BITS
    ScopedClearErrno clear_errno;
    ThreadLocalBooleanAutoReset flag(g_unchecked_alloc.Pointer(), true);
#endif  // ARCH_CPU_32_BITS
    *result = g_old_malloc(malloc_default_zone(), size);
  } else {
    *result = malloc(size);
  }

  return *result != NULL;
}

bool UncheckedCalloc(size_t num_items, size_t size, void** result) {
  if (g_old_calloc) {
#if ARCH_CPU_32_BITS
    ScopedClearErrno clear_errno;
    ThreadLocalBooleanAutoReset flag(g_unchecked_alloc.Pointer(), true);
#endif  // ARCH_CPU_32_BITS
    *result = g_old_calloc(malloc_default_zone(), num_items, size);
  } else {
    *result = calloc(num_items, size);
  }

  return *result != NULL;
}

void* UncheckedMalloc(size_t size) {
  void* address;
  return UncheckedMalloc(size, &address) ? address : NULL;
}

void* UncheckedCalloc(size_t num_items, size_t size) {
  void* address;
  return UncheckedCalloc(num_items, size, &address) ? address : NULL;
}

void EnableTerminationOnOutOfMemory() {
  if (g_oom_killer_enabled)
    return;

  g_oom_killer_enabled = true;

  // === C malloc/calloc/valloc/realloc/posix_memalign ===

  // This approach is not perfect, as requests for amounts of memory larger than
  // MALLOC_ABSOLUTE_MAX_SIZE (currently SIZE_T_MAX - (2 * PAGE_SIZE)) will
  // still fail with a NULL rather than dying (see
  // http://opensource.apple.com/source/Libc/Libc-583/gen/malloc.c for details).
  // Unfortunately, it's the best we can do. Also note that this does not affect
  // allocations from non-default zones.

  CHECK(!g_old_malloc && !g_old_calloc && !g_old_valloc && !g_old_realloc &&
        !g_old_memalign) << "Old allocators unexpectedly non-null";

  CHECK(!g_old_malloc_purgeable && !g_old_calloc_purgeable &&
        !g_old_valloc_purgeable && !g_old_realloc_purgeable &&
        !g_old_memalign_purgeable) << "Old allocators unexpectedly non-null";

#if !defined(ADDRESS_SANITIZER)
  // Don't do anything special on OOM for the malloc zones replaced by
  // AddressSanitizer, as modifying or protecting them may not work correctly.

  ChromeMallocZone* default_zone =
      reinterpret_cast<ChromeMallocZone*>(malloc_default_zone());
  ChromeMallocZone* purgeable_zone =
      reinterpret_cast<ChromeMallocZone*>(malloc_default_purgeable_zone());

  mach_vm_address_t default_reprotection_start = 0;
  mach_vm_size_t default_reprotection_length = 0;
  vm_prot_t default_reprotection_value = VM_PROT_NONE;
  DeprotectMallocZone(default_zone,
                      &default_reprotection_start,
                      &default_reprotection_length,
                      &default_reprotection_value);

  mach_vm_address_t purgeable_reprotection_start = 0;
  mach_vm_size_t purgeable_reprotection_length = 0;
  vm_prot_t purgeable_reprotection_value = VM_PROT_NONE;
  if (purgeable_zone) {
    DeprotectMallocZone(purgeable_zone,
                        &purgeable_reprotection_start,
                        &purgeable_reprotection_length,
                        &purgeable_reprotection_value);
  }

  // Default zone

  g_old_malloc = default_zone->malloc;
  g_old_calloc = default_zone->calloc;
  g_old_valloc = default_zone->valloc;
  g_old_free = default_zone->free;
  g_old_realloc = default_zone->realloc;
  CHECK(g_old_malloc && g_old_calloc && g_old_valloc && g_old_free &&
        g_old_realloc)
      << "Failed to get system allocation functions.";

  default_zone->malloc = oom_killer_malloc;
  default_zone->calloc = oom_killer_calloc;
  default_zone->valloc = oom_killer_valloc;
  default_zone->free = oom_killer_free;
  default_zone->realloc = oom_killer_realloc;

  if (default_zone->version >= 5) {
    g_old_memalign = default_zone->memalign;
    if (g_old_memalign)
      default_zone->memalign = oom_killer_memalign;
  }

  // Purgeable zone (if it exists)

  if (purgeable_zone) {
    g_old_malloc_purgeable = purgeable_zone->malloc;
    g_old_calloc_purgeable = purgeable_zone->calloc;
    g_old_valloc_purgeable = purgeable_zone->valloc;
    g_old_free_purgeable = purgeable_zone->free;
    g_old_realloc_purgeable = purgeable_zone->realloc;
    CHECK(g_old_malloc_purgeable && g_old_calloc_purgeable &&
          g_old_valloc_purgeable && g_old_free_purgeable &&
          g_old_realloc_purgeable)
        << "Failed to get system allocation functions.";

    purgeable_zone->malloc = oom_killer_malloc_purgeable;
    purgeable_zone->calloc = oom_killer_calloc_purgeable;
    purgeable_zone->valloc = oom_killer_valloc_purgeable;
    purgeable_zone->free = oom_killer_free_purgeable;
    purgeable_zone->realloc = oom_killer_realloc_purgeable;

    if (purgeable_zone->version >= 5) {
      g_old_memalign_purgeable = purgeable_zone->memalign;
      if (g_old_memalign_purgeable)
        purgeable_zone->memalign = oom_killer_memalign_purgeable;
    }
  }

  // Restore protection if it was active.

  if (default_reprotection_start) {
    kern_return_t result = mach_vm_protect(mach_task_self(),
                                           default_reprotection_start,
                                           default_reprotection_length,
                                           false,
                                           default_reprotection_value);
    MACH_CHECK(result == KERN_SUCCESS, result) << "mach_vm_protect";
  }

  if (purgeable_reprotection_start) {
    kern_return_t result = mach_vm_protect(mach_task_self(),
                                           purgeable_reprotection_start,
                                           purgeable_reprotection_length,
                                           false,
                                           purgeable_reprotection_value);
    MACH_CHECK(result == KERN_SUCCESS, result) << "mach_vm_protect";
  }
#endif

  // === C malloc_zone_batch_malloc ===

  // batch_malloc is omitted because the default malloc zone's implementation
  // only supports batch_malloc for "tiny" allocations from the free list. It
  // will fail for allocations larger than "tiny", and will only allocate as
  // many blocks as it's able to from the free list. These factors mean that it
  // can return less than the requested memory even in a non-out-of-memory
  // situation. There's no good way to detect whether a batch_malloc failure is
  // due to these other factors, or due to genuine memory or address space
  // exhaustion. The fact that it only allocates space from the "tiny" free list
  // means that it's likely that a failure will not be due to memory exhaustion.
  // Similarly, these constraints on batch_malloc mean that callers must always
  // be expecting to receive less memory than was requested, even in situations
  // where memory pressure is not a concern. Finally, the only public interface
  // to batch_malloc is malloc_zone_batch_malloc, which is specific to the
  // system's malloc implementation. It's unlikely that anyone's even heard of
  // it.

  // === C++ operator new ===

  // Yes, operator new does call through to malloc, but this will catch failures
  // that our imperfect handling of malloc cannot.

  std::set_new_handler(oom_killer_new);

#ifndef ADDRESS_SANITIZER
  // === Core Foundation CFAllocators ===

  // This will not catch allocation done by custom allocators, but will catch
  // all allocation done by system-provided ones.

  CHECK(!g_old_cfallocator_system_default && !g_old_cfallocator_malloc &&
        !g_old_cfallocator_malloc_zone)
      << "Old allocators unexpectedly non-null";

  bool cf_allocator_internals_known = CanGetContextForCFAllocator();

  if (cf_allocator_internals_known) {
    CFAllocatorContext* context =
        ContextForCFAllocator(kCFAllocatorSystemDefault);
    CHECK(context) << "Failed to get context for kCFAllocatorSystemDefault.";
    g_old_cfallocator_system_default = context->allocate;
    CHECK(g_old_cfallocator_system_default)
        << "Failed to get kCFAllocatorSystemDefault allocation function.";
    context->allocate = oom_killer_cfallocator_system_default;

    context = ContextForCFAllocator(kCFAllocatorMalloc);
    CHECK(context) << "Failed to get context for kCFAllocatorMalloc.";
    g_old_cfallocator_malloc = context->allocate;
    CHECK(g_old_cfallocator_malloc)
        << "Failed to get kCFAllocatorMalloc allocation function.";
    context->allocate = oom_killer_cfallocator_malloc;

    context = ContextForCFAllocator(kCFAllocatorMallocZone);
    CHECK(context) << "Failed to get context for kCFAllocatorMallocZone.";
    g_old_cfallocator_malloc_zone = context->allocate;
    CHECK(g_old_cfallocator_malloc_zone)
        << "Failed to get kCFAllocatorMallocZone allocation function.";
    context->allocate = oom_killer_cfallocator_malloc_zone;
  } else {
    DLOG(WARNING) << "Internals of CFAllocator not known; out-of-memory "
                     "failures via CFAllocator will not result in termination. "
                     "http://crbug.com/45650";
  }
#endif

  // === Cocoa NSObject allocation ===

  // Note that both +[NSObject new] and +[NSObject alloc] call through to
  // +[NSObject allocWithZone:].

  CHECK(!g_old_allocWithZone)
      << "Old allocator unexpectedly non-null";

  Class nsobject_class = [NSObject class];
  Method orig_method = class_getClassMethod(nsobject_class,
                                            @selector(allocWithZone:));
  g_old_allocWithZone = reinterpret_cast<allocWithZone_t>(
      method_getImplementation(orig_method));
  CHECK(g_old_allocWithZone)
      << "Failed to get allocWithZone allocation function.";
  method_setImplementation(orig_method,
                           reinterpret_cast<IMP>(oom_killer_allocWithZone));
}

}  // namespace base
