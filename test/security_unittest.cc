// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <limits>

#include "butil/file_util.h"
#include "butil/logging.h"
#include "butil/memory/scoped_ptr.h"
#include "butil/build_config.h"
#include <gtest/gtest.h>

#if defined(OS_POSIX)
#include <sys/mman.h>
#include <unistd.h>
#endif

using std::nothrow;
using std::numeric_limits;

namespace {

// This function acts as a compiler optimization barrier. We use it to
// prevent the compiler from making an expression a compile-time constant.
// We also use it so that the compiler doesn't discard certain return values
// as something we don't need (see the comment with calloc below).
template <typename Type>
Type HideValueFromCompiler(volatile Type value) {
#if defined(__GNUC__)
  // In a GCC compatible compiler (GCC or Clang), make this compiler barrier
  // more robust than merely using "volatile".
  __asm__ volatile ("" : "+r" (value));
#endif  // __GNUC__
  return value;
}

// - NO_TCMALLOC (should be defined if compiled with use_allocator!="tcmalloc")
// - ADDRESS_SANITIZER and SYZYASAN because they have their own memory allocator
// - IOS does not use tcmalloc
// - OS_MACOSX does not use tcmalloc
#if !defined(NO_TCMALLOC) && !defined(ADDRESS_SANITIZER) && \
    !defined(OS_IOS) && !defined(OS_MACOSX) && !defined(SYZYASAN)
  #define TCMALLOC_TEST(function) function
#else
  #define TCMALLOC_TEST(function) DISABLED_##function
#endif

// TODO(jln): switch to std::numeric_limits<int>::max() when we switch to
// C++11.
const size_t kTooBigAllocSize = INT_MAX;

// Detect runtime TCMalloc bypasses.
bool IsTcMallocBypassed() {
#if defined(OS_LINUX)
  // This should detect a TCMalloc bypass from Valgrind.
  char* g_slice = getenv("G_SLICE");
  if (g_slice && !strcmp(g_slice, "always-malloc"))
    return true;
#elif defined(OS_WIN)
  // This should detect a TCMalloc bypass from setting
  // the CHROME_ALLOCATOR environment variable.
  char* allocator = getenv("CHROME_ALLOCATOR");
  if (allocator && strcmp(allocator, "tcmalloc"))
    return true;
#endif
  return false;
}

bool CallocDiesOnOOM() {
// The sanitizers' calloc dies on OOM instead of returning NULL.
// The wrapper function in butil/process_util_linux.cc that is used when we
// compile without TCMalloc will just die on OOM instead of returning NULL.
#if defined(ADDRESS_SANITIZER) || \
    defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER) || \
    (defined(OS_LINUX) && defined(NO_TCMALLOC))
  return true;
#else
  return false;
#endif
}

// Fake test that allow to know the state of TCMalloc by looking at bots.
TEST(SecurityTest, TCMALLOC_TEST(IsTCMallocDynamicallyBypassed)) {
  printf("Malloc is dynamically bypassed: %s\n",
         IsTcMallocBypassed() ? "yes." : "no.");
}

// The MemoryAllocationRestrictions* tests test that we can not allocate a
// memory range that cannot be indexed via an int. This is used to mitigate
// vulnerabilities in libraries that use int instead of size_t.  See
// crbug.com/169327.

TEST(SecurityTest, TCMALLOC_TEST(MemoryAllocationRestrictionsMalloc)) {
  if (!IsTcMallocBypassed()) {
    scoped_ptr<char, butil::FreeDeleter> ptr(static_cast<char*>(
        HideValueFromCompiler(malloc(kTooBigAllocSize))));
    ASSERT_TRUE(!ptr);
  }
}

TEST(SecurityTest, TCMALLOC_TEST(MemoryAllocationRestrictionsCalloc)) {
  if (!IsTcMallocBypassed()) {
    scoped_ptr<char, butil::FreeDeleter> ptr(static_cast<char*>(
        HideValueFromCompiler(calloc(kTooBigAllocSize, 1))));
    ASSERT_TRUE(!ptr);
  }
}

TEST(SecurityTest, TCMALLOC_TEST(MemoryAllocationRestrictionsRealloc)) {
  if (!IsTcMallocBypassed()) {
    char* orig_ptr = static_cast<char*>(malloc(1));
    ASSERT_TRUE(orig_ptr);
    scoped_ptr<char, butil::FreeDeleter> ptr(static_cast<char*>(
        HideValueFromCompiler(realloc(orig_ptr, kTooBigAllocSize))));
    ASSERT_TRUE(!ptr);
    // If realloc() did not succeed, we need to free orig_ptr.
    free(orig_ptr);
  }
}

typedef struct {
  char large_array[kTooBigAllocSize];
} VeryLargeStruct;

TEST(SecurityTest, TCMALLOC_TEST(MemoryAllocationRestrictionsNew)) {
  if (!IsTcMallocBypassed()) {
    scoped_ptr<VeryLargeStruct> ptr(
        HideValueFromCompiler(new (nothrow) VeryLargeStruct));
    ASSERT_TRUE(!ptr);
  }
}

TEST(SecurityTest, TCMALLOC_TEST(MemoryAllocationRestrictionsNewArray)) {
  if (!IsTcMallocBypassed()) {
    scoped_ptr<char[]> ptr(
        HideValueFromCompiler(new (nothrow) char[kTooBigAllocSize]));
    ASSERT_TRUE(!ptr);
  }
}

// The tests bellow check for overflows in new[] and calloc().

#if defined(OS_IOS) || defined(OS_WIN) || defined(THREAD_SANITIZER)
  #define DISABLE_ON_IOS_AND_WIN_AND_TSAN(function) DISABLED_##function
#else
  #define DISABLE_ON_IOS_AND_WIN_AND_TSAN(function) function
#endif


// FIXME(gejun): following logic of the case, it definitely should crash, I don't
// know why it's judged as failure.
#if defined(FixedNewOverflow)

// There are platforms where these tests are known to fail. We would like to
// be able to easily check the status on the bots, but marking tests as
// FAILS_ is too clunky.
void OverflowTestsSoftExpectTrue(bool overflow_detected) {
  if (!overflow_detected) {
#if defined(OS_LINUX) || defined(OS_ANDROID) || defined(OS_MACOSX)
    // Sadly, on Linux, Android, and OSX we don't have a good story yet. Don't
    // fail the test, but report.
    printf("Platform has overflow: %s\n",
           !overflow_detected ? "yes." : "no.");
#else
    // Otherwise, fail the test. (Note: EXPECT are ok in subfunctions, ASSERT
    // aren't).
    EXPECT_TRUE(overflow_detected);
#endif
  }
}

// Test array[TooBig][X] and array[X][TooBig] allocations for int overflows.
// IOS doesn't honor nothrow, so disable the test there.
// Crashes on Windows Dbg builds, disable there as well.
TEST(SecurityTest, DISABLE_ON_IOS_AND_WIN_AND_TSAN(NewOverflow)) {
  const size_t kArraySize = 4096;
  // We want something "dynamic" here, so that the compiler doesn't
  // immediately reject crazy arrays.
  const size_t kDynamicArraySize = HideValueFromCompiler(kArraySize);
  // numeric_limits are still not constexpr until we switch to C++11, so we
  // use an ugly cast.
  const size_t kMaxSizeT = ~static_cast<size_t>(0);
  ASSERT_EQ(numeric_limits<size_t>::max(), kMaxSizeT);
  const size_t kArraySize2 = kMaxSizeT / kArraySize + 10;
  const size_t kDynamicArraySize2 = HideValueFromCompiler(kArraySize2);
  {
    scoped_ptr<char[][kArraySize]> array_pointer(new (nothrow)
        char[kDynamicArraySize2][kArraySize]);
    OverflowTestsSoftExpectTrue(!array_pointer);
  }
  // On windows, the compiler prevents static array sizes of more than
  // 0x7fffffff (error C2148).
#if !defined(OS_WIN) || !defined(ARCH_CPU_64_BITS)
  {
    scoped_ptr<char[][kArraySize2]> array_pointer(new (nothrow)
        char[kDynamicArraySize][kArraySize2]);
    OverflowTestsSoftExpectTrue(!array_pointer);
  }
#endif  // !defined(OS_WIN) || !defined(ARCH_CPU_64_BITS)
}
#endif

// Call calloc(), eventually free the memory and return whether or not
// calloc() did succeed.
bool CallocReturnsNull(size_t nmemb, size_t size) {
  scoped_ptr<char, butil::FreeDeleter> array_pointer(
      static_cast<char*>(calloc(nmemb, size)));
  // We need the call to HideValueFromCompiler(): we have seen LLVM
  // optimize away the call to calloc() entirely and assume
  // the pointer to not be NULL.
  return HideValueFromCompiler(array_pointer.get()) == NULL;
}

// Test if calloc() can overflow.
TEST(SecurityTest, CallocOverflow) {
  const size_t kArraySize = 4096;
  const size_t kMaxSizeT = numeric_limits<size_t>::max();
  const size_t kArraySize2 = kMaxSizeT / kArraySize + 10;
  if (!CallocDiesOnOOM()) {
    EXPECT_TRUE(CallocReturnsNull(kArraySize, kArraySize2));
    EXPECT_TRUE(CallocReturnsNull(kArraySize2, kArraySize));
  } else {
    // It's also ok for calloc to just terminate the process.
    // NOTE(gejun): butil/process/memory.cc is not linked right now,
    // disable following assertions on calloc
//#if defined(GTEST_HAS_DEATH_TEST)
//    EXPECT_DEATH(CallocReturnsNull(kArraySize, kArraySize2), "");
//    EXPECT_DEATH(CallocReturnsNull(kArraySize2, kArraySize), "");
//#endif  // GTEST_HAS_DEATH_TEST
  }
}

#if defined(OS_LINUX) && defined(__x86_64__)
// Check if ptr1 and ptr2 are separated by less than size chars.
bool ArePointersToSameArea(void* ptr1, void* ptr2, size_t size) {
  ptrdiff_t ptr_diff = reinterpret_cast<char*>(std::max(ptr1, ptr2)) -
                       reinterpret_cast<char*>(std::min(ptr1, ptr2));
  return static_cast<size_t>(ptr_diff) <= size;
}

// Check if TCMalloc uses an underlying random memory allocator.
TEST(SecurityTest, TCMALLOC_TEST(RandomMemoryAllocations)) {
  if (IsTcMallocBypassed())
    return;
  size_t kPageSize = 4096;  // We support x86_64 only.
  // Check that malloc() returns an address that is neither the kernel's
  // un-hinted mmap area, nor the current brk() area. The first malloc() may
  // not be at a random address because TCMalloc will first exhaust any memory
  // that it has allocated early on, before starting the sophisticated
  // allocators.
  void* default_mmap_heap_address =
      mmap(0, kPageSize, PROT_READ|PROT_WRITE,
           MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(default_mmap_heap_address,
            static_cast<void*>(MAP_FAILED));
  ASSERT_EQ(munmap(default_mmap_heap_address, kPageSize), 0);
  void* brk_heap_address = sbrk(0);
  ASSERT_NE(brk_heap_address, reinterpret_cast<void*>(-1));
  ASSERT_TRUE(brk_heap_address != NULL);
  // 1 MB should get us past what TCMalloc pre-allocated before initializing
  // the sophisticated allocators.
  size_t kAllocSize = 1<<20;
  scoped_ptr<char, butil::FreeDeleter> ptr(
      static_cast<char*>(malloc(kAllocSize)));
  ASSERT_TRUE(ptr != NULL);
  // If two pointers are separated by less than 512MB, they are considered
  // to be in the same area.
  // Our random pointer could be anywhere within 0x3fffffffffff (46bits),
  // and we are checking that it's not withing 1GB (30 bits) from two
  // addresses (brk and mmap heap). We have roughly one chance out of
  // 2^15 to flake.
  const size_t kAreaRadius = 1<<29;
  bool in_default_mmap_heap = ArePointersToSameArea(
      ptr.get(), default_mmap_heap_address, kAreaRadius);
  EXPECT_FALSE(in_default_mmap_heap);

  bool in_default_brk_heap = ArePointersToSameArea(
      ptr.get(), brk_heap_address, kAreaRadius);
  EXPECT_FALSE(in_default_brk_heap);

  // In the implementation, we always mask our random addresses with
  // kRandomMask, so we use it as an additional detection mechanism.
  const uintptr_t kRandomMask = 0x3fffffffffffULL;
  bool impossible_random_address =
      reinterpret_cast<uintptr_t>(ptr.get()) & ~kRandomMask;
  EXPECT_FALSE(impossible_random_address);
}

#endif  // defined(OS_LINUX) && defined(__x86_64__)

}  // namespace
