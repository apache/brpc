// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/basictypes.h"
#include "butil/memory/scoped_ptr.h"
#include "butil/memory/shared_memory.h"
#include "butil/process/kill.h"
#include "butil/rand_util.h"
#include "butil/strings/string_number_conversions.h"
#include "butil/sys_info.h"
#include "multiprocess_test.h"
#include "butil/threading/platform_thread.h"
#include "butil/time/time.h"
#include <gtest/gtest.h>
#include "multiprocess_func_list.h"

#if defined(OS_MACOSX)
#include "butil/mac/scoped_nsautorelease_pool.h"
#endif

#if defined(OS_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(OS_WIN)
#include "butil/win/scoped_handle.h"
#endif

static const int kNumThreads = 5;
#if !defined(OS_IOS)  // iOS does not allow multiple processes.
static const int kNumTasks = 5;
#endif

namespace butil {

namespace {

// Each thread will open the shared memory.  Each thread will take a different 4
// byte int pointer, and keep changing it, with some small pauses in between.
// Verify that each thread's value in the shared memory is always correct.
class MultipleThreadMain : public PlatformThread::Delegate {
 public:
  explicit MultipleThreadMain(int16_t id) : id_(id) {}
  virtual ~MultipleThreadMain() {}

  static void CleanUp() {
    SharedMemory memory;
    memory.Delete(s_test_name_);
  }

  // PlatformThread::Delegate interface.
  virtual void ThreadMain() OVERRIDE {
#if defined(OS_MACOSX)
    mac::ScopedNSAutoreleasePool pool;
#endif
    const uint32_t kDataSize = 1024;
    SharedMemory memory;
    bool rv = memory.CreateNamedDeprecated(s_test_name_, true, kDataSize);
    EXPECT_TRUE(rv);
    rv = memory.Map(kDataSize);
    EXPECT_TRUE(rv);
    int *ptr = static_cast<int*>(memory.memory()) + id_;
    EXPECT_EQ(0, *ptr);

    for (int idx = 0; idx < 100; idx++) {
      *ptr = idx;
      PlatformThread::Sleep(butil::TimeDelta::FromMilliseconds(1));
      EXPECT_EQ(*ptr, idx);
    }
    // Reset back to 0 for the next test that uses the same name.
    *ptr = 0;

    memory.Close();
  }

 private:
  int16_t id_;

  static const char* const s_test_name_;

  DISALLOW_COPY_AND_ASSIGN(MultipleThreadMain);
};

const char* const MultipleThreadMain::s_test_name_ =
    "SharedMemoryOpenThreadTest";

// TODO(port):
// This test requires the ability to pass file descriptors between processes.
// We haven't done that yet in Chrome for POSIX.
#if defined(OS_WIN)
// Each thread will open the shared memory.  Each thread will take the memory,
// and keep changing it while trying to lock it, with some small pauses in
// between. Verify that each thread's value in the shared memory is always
// correct.
class MultipleLockThread : public PlatformThread::Delegate {
 public:
  explicit MultipleLockThread(int id) : id_(id) {}
  virtual ~MultipleLockThread() {}

  // PlatformThread::Delegate interface.
  virtual void ThreadMain() OVERRIDE {
    const uint32_t kDataSize = sizeof(int);
    SharedMemoryHandle handle = NULL;
    {
      SharedMemory memory1;
      EXPECT_TRUE(memory1.CreateNamedDeprecated(
          "SharedMemoryMultipleLockThreadTest", true, kDataSize));
      EXPECT_TRUE(memory1.ShareToProcess(GetCurrentProcess(), &handle));
      // TODO(paulg): Implement this once we have a posix version of
      // SharedMemory::ShareToProcess.
      EXPECT_TRUE(true);
    }

    SharedMemory memory2(handle, false);
    EXPECT_TRUE(memory2.Map(kDataSize));
    volatile int* const ptr = static_cast<int*>(memory2.memory());

    for (int idx = 0; idx < 20; idx++) {
      memory2.LockDeprecated();
      int i = (id_ << 16) + idx;
      *ptr = i;
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(1));
      EXPECT_EQ(*ptr, i);
      memory2.UnlockDeprecated();
    }

    memory2.Close();
  }

 private:
  int id_;

  DISALLOW_COPY_AND_ASSIGN(MultipleLockThread);
};
#endif

}  // namespace

// Android doesn't support SharedMemory::Open/Delete/
// CreateNamedDeprecated(openExisting=true)
#if !defined(OS_ANDROID)
TEST(SharedMemoryTest, OpenClose) {
  const uint32_t kDataSize = 1024;
  std::string test_name = "SharedMemoryOpenCloseTest";

  // Open two handles to a memory segment, confirm that they are mapped
  // separately yet point to the same space.
  SharedMemory memory1;
  bool rv = memory1.Delete(test_name);
  EXPECT_TRUE(rv);
  rv = memory1.Delete(test_name);
  EXPECT_TRUE(rv);
  rv = memory1.Open(test_name, false);
  EXPECT_FALSE(rv);
  rv = memory1.CreateNamedDeprecated(test_name, false, kDataSize);
  EXPECT_TRUE(rv);
  rv = memory1.Map(kDataSize);
  EXPECT_TRUE(rv);
  SharedMemory memory2;
  rv = memory2.Open(test_name, false);
  EXPECT_TRUE(rv);
  rv = memory2.Map(kDataSize);
  EXPECT_TRUE(rv);
  EXPECT_NE(memory1.memory(), memory2.memory());  // Compare the pointers.

  // Make sure we don't segfault. (it actually happened!)
  ASSERT_NE(memory1.memory(), static_cast<void*>(NULL));
  ASSERT_NE(memory2.memory(), static_cast<void*>(NULL));

  // Write data to the first memory segment, verify contents of second.
  memset(memory1.memory(), '1', kDataSize);
  EXPECT_EQ(memcmp(memory1.memory(), memory2.memory(), kDataSize), 0);

  // Close the first memory segment, and verify the second has the right data.
  memory1.Close();
  char *start_ptr = static_cast<char *>(memory2.memory());
  char *end_ptr = start_ptr + kDataSize;
  for (char* ptr = start_ptr; ptr < end_ptr; ptr++)
    EXPECT_EQ(*ptr, '1');

  // Close the second memory segment.
  memory2.Close();

  rv = memory1.Delete(test_name);
  EXPECT_TRUE(rv);
  rv = memory2.Delete(test_name);
  EXPECT_TRUE(rv);
}

TEST(SharedMemoryTest, OpenExclusive) {
  const uint32_t kDataSize = 1024;
  const uint32_t kDataSize2 = 2048;
  std::ostringstream test_name_stream;
  test_name_stream << "SharedMemoryOpenExclusiveTest."
                   << Time::Now().ToDoubleT();
  std::string test_name = test_name_stream.str();

  // Open two handles to a memory segment and check that
  // open_existing_deprecated works as expected.
  SharedMemory memory1;
  bool rv = memory1.CreateNamedDeprecated(test_name, false, kDataSize);
  EXPECT_TRUE(rv);

  // Memory1 knows it's size because it created it.
  EXPECT_EQ(memory1.requested_size(), kDataSize);

  rv = memory1.Map(kDataSize);
  EXPECT_TRUE(rv);

  // The mapped memory1 must be at least the size we asked for.
  EXPECT_GE(memory1.mapped_size(), kDataSize);

  // The mapped memory1 shouldn't exceed rounding for allocation granularity.
  EXPECT_LT(memory1.mapped_size(),
            kDataSize + butil::SysInfo::VMAllocationGranularity());

  memset(memory1.memory(), 'G', kDataSize);

  SharedMemory memory2;
  // Should not be able to create if openExisting is false.
  rv = memory2.CreateNamedDeprecated(test_name, false, kDataSize2);
  EXPECT_FALSE(rv);

  // Should be able to create with openExisting true.
  rv = memory2.CreateNamedDeprecated(test_name, true, kDataSize2);
  EXPECT_TRUE(rv);

  // Memory2 shouldn't know the size because we didn't create it.
  EXPECT_EQ(memory2.requested_size(), 0U);

  // We should be able to map the original size.
  rv = memory2.Map(kDataSize);
  EXPECT_TRUE(rv);

  // The mapped memory2 must be at least the size of the original.
  EXPECT_GE(memory2.mapped_size(), kDataSize);

  // The mapped memory2 shouldn't exceed rounding for allocation granularity.
  EXPECT_LT(memory2.mapped_size(),
            kDataSize2 + butil::SysInfo::VMAllocationGranularity());

  // Verify that opening memory2 didn't truncate or delete memory 1.
  char *start_ptr = static_cast<char *>(memory2.memory());
  char *end_ptr = start_ptr + kDataSize;
  for (char* ptr = start_ptr; ptr < end_ptr; ptr++) {
    EXPECT_EQ(*ptr, 'G');
  }

  memory1.Close();
  memory2.Close();

  rv = memory1.Delete(test_name);
  EXPECT_TRUE(rv);
}
#endif

// Create a set of N threads to each open a shared memory segment and write to
// it. Verify that they are always reading/writing consistent data.
TEST(SharedMemoryTest, MultipleThreads) {
  MultipleThreadMain::CleanUp();
  // On POSIX we have a problem when 2 threads try to create the shmem
  // (a file) at exactly the same time, since create both creates the
  // file and zerofills it.  We solve the problem for this unit test
  // (make it not flaky) by starting with 1 thread, then
  // intentionally don't clean up its shmem before running with
  // kNumThreads.

  int threadcounts[] = { 1, kNumThreads };
  for (size_t i = 0; i < arraysize(threadcounts); i++) {
    int numthreads = threadcounts[i];
    scoped_ptr<PlatformThreadHandle[]> thread_handles;
    scoped_ptr<MultipleThreadMain*[]> thread_delegates;

    thread_handles.reset(new PlatformThreadHandle[numthreads]);
    thread_delegates.reset(new MultipleThreadMain*[numthreads]);

    // Spawn the threads.
    for (int16_t index = 0; index < numthreads; index++) {
      PlatformThreadHandle pth;
      thread_delegates[index] = new MultipleThreadMain(index);
      EXPECT_TRUE(PlatformThread::Create(0, thread_delegates[index], &pth));
      thread_handles[index] = pth;
    }

    // Wait for the threads to finish.
    for (int index = 0; index < numthreads; index++) {
      PlatformThread::Join(thread_handles[index]);
      delete thread_delegates[index];
    }
  }
  MultipleThreadMain::CleanUp();
}

// TODO(port): this test requires the MultipleLockThread class
// (defined above), which requires the ability to pass file
// descriptors between processes.  We haven't done that yet in Chrome
// for POSIX.
#if defined(OS_WIN)
// Create a set of threads to each open a shared memory segment and write to it
// with the lock held. Verify that they are always reading/writing consistent
// data.
TEST(SharedMemoryTest, Lock) {
  PlatformThreadHandle thread_handles[kNumThreads];
  MultipleLockThread* thread_delegates[kNumThreads];

  // Spawn the threads.
  for (int index = 0; index < kNumThreads; ++index) {
    PlatformThreadHandle pth;
    thread_delegates[index] = new MultipleLockThread(index);
    EXPECT_TRUE(PlatformThread::Create(0, thread_delegates[index], &pth));
    thread_handles[index] = pth;
  }

  // Wait for the threads to finish.
  for (int index = 0; index < kNumThreads; ++index) {
    PlatformThread::Join(thread_handles[index]);
    delete thread_delegates[index];
  }
}
#endif

// Allocate private (unique) shared memory with an empty string for a
// name.  Make sure several of them don't point to the same thing as
// we might expect if the names are equal.
TEST(SharedMemoryTest, AnonymousPrivate) {
  int i, j;
  int count = 4;
  bool rv;
  const uint32_t kDataSize = 8192;

  scoped_ptr<SharedMemory[]> memories(new SharedMemory[count]);
  scoped_ptr<int*[]> pointers(new int*[count]);
  ASSERT_TRUE(memories.get());
  ASSERT_TRUE(pointers.get());

  for (i = 0; i < count; i++) {
    rv = memories[i].CreateAndMapAnonymous(kDataSize);
    EXPECT_TRUE(rv);
    int *ptr = static_cast<int*>(memories[i].memory());
    EXPECT_TRUE(ptr);
    pointers[i] = ptr;
  }

  for (i = 0; i < count; i++) {
    // zero out the first int in each except for i; for that one, make it 100.
    for (j = 0; j < count; j++) {
      if (i == j)
        pointers[j][0] = 100;
      else
        pointers[j][0] = 0;
    }
    // make sure there is no bleeding of the 100 into the other pointers
    for (j = 0; j < count; j++) {
      if (i == j)
        EXPECT_EQ(100, pointers[j][0]);
      else
        EXPECT_EQ(0, pointers[j][0]);
    }
  }

  for (int i = 0; i < count; i++) {
    memories[i].Close();
  }
}

TEST(SharedMemoryTest, ShareReadOnly) {
  StringPiece contents = "Hello World";

  SharedMemory writable_shmem;
  SharedMemoryCreateOptions options;
  options.size = contents.size();
  options.share_read_only = true;
  ASSERT_TRUE(writable_shmem.Create(options));
  ASSERT_TRUE(writable_shmem.Map(options.size));
  memcpy(writable_shmem.memory(), contents.data(), contents.size());
  EXPECT_TRUE(writable_shmem.Unmap());

  SharedMemoryHandle readonly_handle;
  ASSERT_TRUE(writable_shmem.ShareReadOnlyToProcess(GetCurrentProcessHandle(),
                                                    &readonly_handle));
  SharedMemory readonly_shmem(readonly_handle, /*readonly=*/true);

  ASSERT_TRUE(readonly_shmem.Map(contents.size()));
  EXPECT_EQ(contents,
            StringPiece(static_cast<const char*>(readonly_shmem.memory()),
                        contents.size()));
  EXPECT_TRUE(readonly_shmem.Unmap());

  // Make sure the writable instance is still writable.
  ASSERT_TRUE(writable_shmem.Map(contents.size()));
  StringPiece new_contents = "Goodbye";
  memcpy(writable_shmem.memory(), new_contents.data(), new_contents.size());
  EXPECT_EQ(new_contents,
            StringPiece(static_cast<const char*>(writable_shmem.memory()),
                        new_contents.size()));

  // We'd like to check that if we send the read-only segment to another
  // process, then that other process can't reopen it read/write.  (Since that
  // would be a security hole.)  Setting up multiple processes is hard in a
  // unittest, so this test checks that the *current* process can't reopen the
  // segment read/write.  I think the test here is stronger than we actually
  // care about, but there's a remote possibility that sending a file over a
  // pipe would transform it into read/write.
  SharedMemoryHandle handle = readonly_shmem.handle();

#if defined(OS_ANDROID)
  // The "read-only" handle is still writable on Android:
  // http://crbug.com/320865
  (void)handle;
#elif defined(OS_POSIX)
  EXPECT_EQ(O_RDONLY, fcntl(handle.fd, F_GETFL) & O_ACCMODE)
      << "The descriptor itself should be read-only.";

  errno = 0;
  void* writable = mmap(
      NULL, contents.size(), PROT_READ | PROT_WRITE, MAP_SHARED, handle.fd, 0);
  int mmap_errno = errno;
  EXPECT_EQ(MAP_FAILED, writable)
      << "It shouldn't be possible to re-mmap the descriptor writable.";
  EXPECT_EQ(EACCES, mmap_errno) << strerror(mmap_errno);
  if (writable != MAP_FAILED)
    EXPECT_EQ(0, munmap(writable, readonly_shmem.mapped_size()));

#elif defined(OS_WIN)
  EXPECT_EQ(NULL, MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0))
      << "Shouldn't be able to map memory writable.";

  HANDLE temp_handle;
  BOOL rv = ::DuplicateHandle(GetCurrentProcess(),
                              handle,
                              GetCurrentProcess,
                              &temp_handle,
                              FILE_MAP_ALL_ACCESS,
                              false,
                              0);
  EXPECT_EQ(FALSE, rv)
      << "Shouldn't be able to duplicate the handle into a writable one.";
  if (rv)
    butil::win::ScopedHandle writable_handle(temp_handle);
#else
#error Unexpected platform; write a test that tries to make 'handle' writable.
#endif  // defined(OS_POSIX) || defined(OS_WIN)
}

TEST(SharedMemoryTest, ShareToSelf) {
  StringPiece contents = "Hello World";

  SharedMemory shmem;
  ASSERT_TRUE(shmem.CreateAndMapAnonymous(contents.size()));
  memcpy(shmem.memory(), contents.data(), contents.size());
  EXPECT_TRUE(shmem.Unmap());

  SharedMemoryHandle shared_handle;
  ASSERT_TRUE(shmem.ShareToProcess(GetCurrentProcessHandle(), &shared_handle));
  SharedMemory shared(shared_handle, /*readonly=*/false);

  ASSERT_TRUE(shared.Map(contents.size()));
  EXPECT_EQ(
      contents,
      StringPiece(static_cast<const char*>(shared.memory()), contents.size()));

  ASSERT_TRUE(shmem.ShareToProcess(GetCurrentProcessHandle(), &shared_handle));
  SharedMemory readonly(shared_handle, /*readonly=*/true);

  ASSERT_TRUE(readonly.Map(contents.size()));
  EXPECT_EQ(contents,
            StringPiece(static_cast<const char*>(readonly.memory()),
                        contents.size()));
}

TEST(SharedMemoryTest, MapAt) {
  ASSERT_TRUE(SysInfo::VMAllocationGranularity() >= sizeof(uint32_t));
  const size_t kCount = SysInfo::VMAllocationGranularity();
  const size_t kDataSize = kCount * sizeof(uint32_t);

  SharedMemory memory;
  ASSERT_TRUE(memory.CreateAndMapAnonymous(kDataSize));
  uint32_t* ptr = static_cast<uint32_t*>(memory.memory());
  ASSERT_NE(ptr, static_cast<void*>(NULL));

  for (size_t i = 0; i < kCount; ++i) {
    ptr[i] = i;
  }

  memory.Unmap();

  off_t offset = SysInfo::VMAllocationGranularity();
  ASSERT_TRUE(memory.MapAt(offset, kDataSize - offset));
  offset /= sizeof(uint32_t);
  ptr = static_cast<uint32_t*>(memory.memory());
  ASSERT_NE(ptr, static_cast<void*>(NULL));
  for (size_t i = offset; i < kCount; ++i) {
    EXPECT_EQ(ptr[i - offset], i);
  }
}

TEST(SharedMemoryTest, MapTwice) {
  const uint32_t kDataSize = 1024;
  SharedMemory memory;
  bool rv = memory.CreateAndMapAnonymous(kDataSize);
  EXPECT_TRUE(rv);

  void* old_address = memory.memory();

  rv = memory.Map(kDataSize);
  EXPECT_FALSE(rv);
  EXPECT_EQ(old_address, memory.memory());
}

#if defined(OS_POSIX)
// Create a shared memory object, mmap it, and mprotect it to PROT_EXEC.
TEST(SharedMemoryTest, AnonymousExecutable) {
  const uint32_t kTestSize = 1 << 16;

  SharedMemory shared_memory;
  SharedMemoryCreateOptions options;
  options.size = kTestSize;
  options.executable = true;

  EXPECT_TRUE(shared_memory.Create(options));
  EXPECT_TRUE(shared_memory.Map(shared_memory.requested_size()));

  EXPECT_EQ(0, mprotect(shared_memory.memory(), shared_memory.requested_size(),
                        PROT_READ | PROT_EXEC));
}

// Android supports a different permission model than POSIX for its "ashmem"
// shared memory implementation. So the tests about file permissions are not
// included on Android.
#if !defined(OS_ANDROID)

// Set a umask and restore the old mask on destruction.
class ScopedUmaskSetter {
 public:
  explicit ScopedUmaskSetter(mode_t target_mask) {
    old_umask_ = umask(target_mask);
  }
  ~ScopedUmaskSetter() { umask(old_umask_); }
 private:
  mode_t old_umask_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(ScopedUmaskSetter);
};

// Create a shared memory object, check its permissions.
TEST(SharedMemoryTest, FilePermissionsAnonymous) {
  const uint32_t kTestSize = 1 << 8;

  SharedMemory shared_memory;
  SharedMemoryCreateOptions options;
  options.size = kTestSize;
  // Set a file mode creation mask that gives all permissions.
  ScopedUmaskSetter permissive_mask(S_IWGRP | S_IWOTH);

  EXPECT_TRUE(shared_memory.Create(options));

  int shm_fd = shared_memory.handle().fd;
  struct stat shm_stat;
  EXPECT_EQ(0, fstat(shm_fd, &shm_stat));
  // Neither the group, nor others should be able to read the shared memory
  // file.
  EXPECT_FALSE(shm_stat.st_mode & S_IRWXO);
  EXPECT_FALSE(shm_stat.st_mode & S_IRWXG);
}

// Create a shared memory object, check its permissions.
TEST(SharedMemoryTest, FilePermissionsNamed) {
  const uint32_t kTestSize = 1 << 8;

  SharedMemory shared_memory;
  SharedMemoryCreateOptions options;
  options.size = kTestSize;
  std::string shared_mem_name = "shared_perm_test-" + IntToString(getpid()) +
      "-" + Uint64ToString(RandUint64());
  options.name_deprecated = &shared_mem_name;
  // Set a file mode creation mask that gives all permissions.
  ScopedUmaskSetter permissive_mask(S_IWGRP | S_IWOTH);

  EXPECT_TRUE(shared_memory.Create(options));
  // Clean-up the backing file name immediately, we don't need it.
  EXPECT_TRUE(shared_memory.Delete(shared_mem_name));

  int shm_fd = shared_memory.handle().fd;
  struct stat shm_stat;
  EXPECT_EQ(0, fstat(shm_fd, &shm_stat));
  // Neither the group, nor others should have been able to open the shared
  // memory file while its name existed.
  EXPECT_FALSE(shm_stat.st_mode & S_IRWXO);
  EXPECT_FALSE(shm_stat.st_mode & S_IRWXG);
}
#endif  // !defined(OS_ANDROID)

#endif  // defined(OS_POSIX)

// Map() will return addresses which are aligned to the platform page size, this
// varies from platform to platform though.  Since we'd like to advertise a
// minimum alignment that callers can count on, test for it here.
TEST(SharedMemoryTest, MapMinimumAlignment) {
  static const int kDataSize = 8192;

  SharedMemory shared_memory;
  ASSERT_TRUE(shared_memory.CreateAndMapAnonymous(kDataSize));
  EXPECT_EQ(0U, reinterpret_cast<uintptr_t>(
      shared_memory.memory()) & (SharedMemory::MAP_MINIMUM_ALIGNMENT - 1));
  shared_memory.Close();
}

#if !defined(OS_IOS)  // iOS does not allow multiple processes.

// On POSIX it is especially important we test shmem across processes,
// not just across threads.  But the test is enabled on all platforms.
class SharedMemoryProcessTest : public MultiProcessTest {
 public:

  static void CleanUp() {
    SharedMemory memory;
    memory.Delete(s_test_name_);
  }

  static int TaskTestMain() {
    int errors = 0;
#if defined(OS_MACOSX)
    mac::ScopedNSAutoreleasePool pool;
#endif
    const uint32_t kDataSize = 1024;
    SharedMemory memory;
    bool rv = memory.CreateNamedDeprecated(s_test_name_, true, kDataSize);
    EXPECT_TRUE(rv);
    if (rv != true)
      errors++;
    rv = memory.Map(kDataSize);
    EXPECT_TRUE(rv);
    if (rv != true)
      errors++;
    int *ptr = static_cast<int*>(memory.memory());

    for (int idx = 0; idx < 20; idx++) {
      memory.LockDeprecated();
      int i = (1 << 16) + idx;
      *ptr = i;
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(10));
      if (*ptr != i)
        errors++;
      memory.UnlockDeprecated();
    }

    memory.Close();
    return errors;
  }

 private:
  static const char* const s_test_name_;
};

const char* const SharedMemoryProcessTest::s_test_name_ = "MPMem";

TEST_F(SharedMemoryProcessTest, Tasks) {
  SharedMemoryProcessTest::CleanUp();

  ProcessHandle handles[kNumTasks];
  for (int index = 0; index < kNumTasks; ++index) {
    handles[index] = SpawnChild("SharedMemoryTestMain");
    ASSERT_TRUE(handles[index]);
  }

  int exit_code = 0;
  for (int index = 0; index < kNumTasks; ++index) {
    EXPECT_TRUE(WaitForExitCode(handles[index], &exit_code));
    EXPECT_EQ(0, exit_code);
  }

  SharedMemoryProcessTest::CleanUp();
}

MULTIPROCESS_TEST_MAIN(SharedMemoryTestMain) {
  return SharedMemoryProcessTest::TaskTestMain();
}

#endif  // !OS_IOS

}  // namespace butil
