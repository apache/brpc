// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/atomicops.h"

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#if defined(THREAD_SANITIZER) || defined(__SANITIZE_THREAD__)
#define BUTIL_ATOMICOPS_TEST_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define BUTIL_ATOMICOPS_TEST_TSAN 1
#endif
#endif

// On non-ARM64 platforms (e.g. Linux x86-64), atomicops.h only switches to
// atomicops_internals_tsan.h when -DTHREAD_SANITIZER is defined; passing only
// -fsanitize=thread leaves inline-assembly CAS and volatile stores invisible to
// TSan.
#if defined(BUTIL_ATOMICOPS_TEST_TSAN) && !defined(THREAD_SANITIZER) && \
    !(defined(ARCH_CPU_ARM64) && defined(COMPILER_GCC))
#define BUTIL_ATOMICOPS_TEST_UNINSTRUMENTED_TSAN 1
#endif

template <class AtomicType>
static void TestAtomicIncrement() {
  // For now, we just test single threaded execution

  // use a guard value to make sure the NoBarrier_AtomicIncrement doesn't go
  // outside the expected address bounds.  This is in particular to
  // test that some future change to the asm code doesn't cause the
  // 32-bit NoBarrier_AtomicIncrement doesn't do the wrong thing on 64-bit
  // machines.
  struct {
    AtomicType prev_word;
    AtomicType count;
    AtomicType next_word;
  } s;

  AtomicType prev_word_value, next_word_value;
  memset(&prev_word_value, 0xFF, sizeof(AtomicType));
  memset(&next_word_value, 0xEE, sizeof(AtomicType));

  s.prev_word = prev_word_value;
  s.count = 0;
  s.next_word = next_word_value;

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 1), 1);
  EXPECT_EQ(s.count, 1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 2), 3);
  EXPECT_EQ(s.count, 3);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 3), 6);
  EXPECT_EQ(s.count, 6);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -3), 3);
  EXPECT_EQ(s.count, 3);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -2), 1);
  EXPECT_EQ(s.count, 1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), -1);
  EXPECT_EQ(s.count, -1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -4), -5);
  EXPECT_EQ(s.count, -5);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 5), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 1), 1);
  EXPECT_EQ(s.count, 1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 2), 3);
  EXPECT_EQ(s.count, 3);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -3), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -5), -5);
  EXPECT_EQ(s.count, -5);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 5), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);
}


#define NUM_BITS(T) (sizeof(T) * 8)


template <class AtomicType>
static void TestCompareAndSwap() {
  AtomicType value = 0;
  AtomicType prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 1);
  EXPECT_EQ(1, value);
  EXPECT_EQ(0, prev);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (static_cast<uint64_t>(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 5);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, prev);

  value = k_test_val;
  prev = butil::subtle::NoBarrier_CompareAndSwap(&value, k_test_val, 5);
  EXPECT_EQ(5, value);
  EXPECT_EQ(k_test_val, prev);

  // Acquire_CompareAndSwap: failure and success paths.
  value = k_test_val;
  prev = butil::subtle::Acquire_CompareAndSwap(&value, 0, 7);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, prev);

  value = k_test_val;
  prev = butil::subtle::Acquire_CompareAndSwap(&value, k_test_val, 7);
  EXPECT_EQ(7, value);
  EXPECT_EQ(k_test_val, prev);

  // Release_CompareAndSwap: failure and success paths.
  value = k_test_val;
  prev = butil::subtle::Release_CompareAndSwap(&value, 0, 9);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, prev);

  value = k_test_val;
  prev = butil::subtle::Release_CompareAndSwap(&value, k_test_val, 9);
  EXPECT_EQ(9, value);
  EXPECT_EQ(k_test_val, prev);
}


template <class AtomicType>
static void TestAtomicExchange() {
  AtomicType value = 0;
  AtomicType new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 1);
  EXPECT_EQ(1, value);
  EXPECT_EQ(0, new_value);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (static_cast<uint64_t>(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  new_value = butil::subtle::NoBarrier_AtomicExchange(&value, k_test_val);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, new_value);

  value = k_test_val;
  new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 5);
  EXPECT_EQ(5, value);
  EXPECT_EQ(k_test_val, new_value);
}


template <class AtomicType>
static void TestAtomicIncrementBounds() {
  // Test at rollover boundary between int_max and int_min
  AtomicType test_val = static_cast<AtomicType>(static_cast<uint64_t>(1)
                                                << (NUM_BITS(AtomicType) - 1));
  AtomicType value = -1 ^ test_val;
  AtomicType new_value = butil::subtle::NoBarrier_AtomicIncrement(&value, 1);
  EXPECT_EQ(test_val, value);
  EXPECT_EQ(value, new_value);

  butil::subtle::NoBarrier_AtomicIncrement(&value, -1);
  EXPECT_EQ(-1 ^ test_val, value);

  // Test at 32-bit boundary for 64-bit atomic type.
  test_val = static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) / 2);
  value = test_val - 1;
  new_value = butil::subtle::NoBarrier_AtomicIncrement(&value, 1);
  EXPECT_EQ(test_val, value);
  EXPECT_EQ(value, new_value);

  butil::subtle::NoBarrier_AtomicIncrement(&value, -1);
  EXPECT_EQ(test_val - 1, value);
}

// Return an AtomicType with the value 0xa5a5a5..
template <class AtomicType>
static AtomicType TestFillValue() {
  AtomicType val = 0;
  memset(&val, 0xa5, sizeof(AtomicType));
  return val;
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestStore() {
  const AtomicType kVal1 = TestFillValue<AtomicType>();
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  butil::subtle::NoBarrier_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::NoBarrier_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);

  butil::subtle::Acquire_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::Acquire_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);

  butil::subtle::Release_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::Release_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestLoad() {
  const AtomicType kVal1 = TestFillValue<AtomicType>();
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::NoBarrier_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::NoBarrier_Load(&value));

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::Acquire_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::Acquire_Load(&value));

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::Release_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::Release_Load(&value));
}

#if !defined(BUTIL_ATOMICOPS_TEST_UNINSTRUMENTED_TSAN)
template <class AtomicType>
static void TestMultiThreadedContention() {
  constexpr int kNumThreads = 8;
  constexpr int kIterations = 2000;

  volatile AtomicType nobarrier_inc = 0;
  volatile AtomicType barrier_inc = 0;
  volatile AtomicType acq_cas_counter = 0;
  volatile AtomicType rel_cas_counter = 0;
  volatile AtomicType spinlock = 0;
  AtomicType protected_counter = 0;

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kIterations; ++i) {
        butil::subtle::NoBarrier_AtomicIncrement(&nobarrier_inc, 1);
        butil::subtle::Barrier_AtomicIncrement(&barrier_inc, 1);

        // Contended Acquire_CompareAndSwap loop.
        AtomicType old_val = butil::subtle::NoBarrier_Load(&acq_cas_counter);
        while (true) {
          AtomicType prev = butil::subtle::Acquire_CompareAndSwap(
              &acq_cas_counter, old_val, old_val + 1);
          if (prev == old_val) {
            break;
          }
          old_val = prev;
        }

        // Contended Release_CompareAndSwap loop.
        old_val = butil::subtle::NoBarrier_Load(&rel_cas_counter);
        while (true) {
          AtomicType prev = butil::subtle::Release_CompareAndSwap(
              &rel_cas_counter, old_val, old_val + 1);
          if (prev == old_val) {
            break;
          }
          old_val = prev;
        }

        // Acquire_CompareAndSwap / Release_Store spinlock protecting non-atomic data.
        while (butil::subtle::Acquire_CompareAndSwap(&spinlock, 0, 1) != 0) {
        }
        ++protected_counter;
        butil::subtle::Release_Store(&spinlock, 0);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  const AtomicType kExpected = kNumThreads * kIterations;
  EXPECT_EQ(kExpected, butil::subtle::NoBarrier_Load(&nobarrier_inc));
  EXPECT_EQ(kExpected, butil::subtle::Acquire_Load(&barrier_inc));
  EXPECT_EQ(kExpected, butil::subtle::Acquire_Load(&acq_cas_counter));
  EXPECT_EQ(kExpected, butil::subtle::Acquire_Load(&rel_cas_counter));
  EXPECT_EQ(kExpected, protected_counter);
}

template <class AtomicType>
static void TestAcquireReleaseVisibility() {
  constexpr int kHandoffs = 2000;
  volatile AtomicType turn = 0;
  uint64_t payload_a = 0;
  uint64_t payload_b = 0;
  std::atomic<bool> producer_done(false);
  std::atomic<bool> consumer_done(false);

  std::thread producer([&]() {
    for (int i = 1; i <= kHandoffs; ++i) {
      while (butil::subtle::Acquire_Load(&turn) == 1) {
        if (consumer_done.load(std::memory_order_acquire)) {
          break;
        }
      }
      if (consumer_done.load(std::memory_order_acquire)) {
        break;
      }
      payload_a = static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL;
      payload_b = ~payload_a;
      if ((i & 1) == 0) {
        const AtomicType prev =
            butil::subtle::Release_CompareAndSwap(&turn, 0, 1);
        if (prev != 0) {
          EXPECT_EQ(static_cast<AtomicType>(0), prev);
          break;
        }
      } else {
        butil::subtle::Release_Store(&turn, 1);
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  for (int i = 1; i <= kHandoffs; ++i) {
    bool producer_exited = false;
    while (butil::subtle::Acquire_Load(&turn) != 1) {
      if (producer_done.load(std::memory_order_acquire) &&
          butil::subtle::Acquire_Load(&turn) != 1) {
        ADD_FAILURE() << "Producer exited before publishing turn=1 at round "
                      << i;
        producer_exited = true;
        break;
      }
    }
    if (producer_exited) {
      break;
    }
    const uint64_t obs_a = payload_a;
    const uint64_t obs_b = payload_b;
    const uint64_t expected_a =
        static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL;
    const uint64_t expected_b = ~expected_a;
    EXPECT_EQ(static_cast<AtomicType>(1), butil::subtle::Release_Load(&turn));
    if (obs_a != expected_a || obs_b != expected_b) {
      EXPECT_EQ(expected_a, obs_a);
      EXPECT_EQ(expected_b, obs_b);
      break;
    }
    if ((i & 1) == 0) {
      const AtomicType prev =
          butil::subtle::Release_CompareAndSwap(&turn, 1, 0);
      if (prev != 1) {
        EXPECT_EQ(static_cast<AtomicType>(1), prev);
        break;
      }
    } else {
      butil::subtle::Release_Store(&turn, 0);
    }
  }

  consumer_done.store(true, std::memory_order_release);
  producer.join();
}
#endif  // !defined(BUTIL_ATOMICOPS_TEST_UNINSTRUMENTED_TSAN)

#if !defined(BUTIL_ATOMICOPS_TEST_TSAN)
template <class AtomicType>
static void TestReleaseCompareAndSwapFailureOrdering() {
  struct alignas(64) PaddedAtomic {
    volatile AtomicType value;
  };

  constexpr int kIterations = 20000;
  PaddedAtomic flag_a{0};
  PaddedAtomic flag_b{0};
  std::atomic<int> round(0);
  std::atomic<int> done(0);
  AtomicType r1 = -1;
  AtomicType r2 = -1;
  int both_zero = 0;

  std::thread t1([&]() {
    for (int i = 1; i <= kIterations; ++i) {
      while (round.load(std::memory_order_acquire) != i) {
      }
      butil::subtle::NoBarrier_Store(&flag_a.value, 1);
      r1 = butil::subtle::Release_CompareAndSwap(&flag_b.value, 1, 2);
      done.fetch_add(1, std::memory_order_release);
    }
  });

  std::thread t2([&]() {
    for (int i = 1; i <= kIterations; ++i) {
      while (round.load(std::memory_order_acquire) != i) {
      }
      butil::subtle::NoBarrier_Store(&flag_b.value, 1);
      r2 = butil::subtle::Release_CompareAndSwap(&flag_a.value, 1, 2);
      done.fetch_add(1, std::memory_order_release);
    }
  });

  for (int i = 1; i <= kIterations; ++i) {
    butil::subtle::NoBarrier_Store(&flag_a.value, 0);
    butil::subtle::NoBarrier_Store(&flag_b.value, 0);
    done.store(0, std::memory_order_relaxed);
    round.store(i, std::memory_order_release);
    while (done.load(std::memory_order_acquire) != 2) {
    }
    if (r1 == 0 && r2 == 0) {
      ++both_zero;
    }
  }

  t1.join();
  t2.join();
  EXPECT_EQ(0, both_zero);
}
#endif

#if defined(ARCH_CPU_ARM64) && defined(COMPILER_GCC) && !defined(THREAD_SANITIZER)
TEST(AtomicOpsTest, UsesArm64GccInternalsHeader) {
#if !defined(BUTIL_ATOMICOPS_INTERNALS_ARM64_GCC_H_)
  FAIL() << "Expected BUTIL_ATOMICOPS_INTERNALS_ARM64_GCC_H_ to be included on ARCH_CPU_ARM64";
#endif
}
#endif

TEST(AtomicOpsTest, Inc) {
  TestAtomicIncrement<butil::subtle::Atomic32>();
  TestAtomicIncrement<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestAtomicIncrement<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, CompareAndSwap) {
  TestCompareAndSwap<butil::subtle::Atomic32>();
  TestCompareAndSwap<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestCompareAndSwap<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, Exchange) {
  TestAtomicExchange<butil::subtle::Atomic32>();
  TestAtomicExchange<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestAtomicExchange<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, IncrementBounds) {
  TestAtomicIncrementBounds<butil::subtle::Atomic32>();
  TestAtomicIncrementBounds<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestAtomicIncrementBounds<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, Store) {
  TestStore<butil::subtle::Atomic32>();
  TestStore<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestStore<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, Load) {
  TestLoad<butil::subtle::Atomic32>();
  TestLoad<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestLoad<butil::subtle::Atomic64>();
#endif
}

TEST(AtomicOpsTest, MultiThreadedContention) {
#if defined(BUTIL_ATOMICOPS_TEST_UNINSTRUMENTED_TSAN)
  GTEST_SKIP() << "Platform atomicops backend uses inline assembly/volatile "
                  "without -DTHREAD_SANITIZER";
#else
  TestMultiThreadedContention<butil::subtle::Atomic32>();
  TestMultiThreadedContention<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestMultiThreadedContention<butil::subtle::Atomic64>();
#endif
#endif
}

TEST(AtomicOpsTest, AcquireReleaseVisibility) {
#if defined(BUTIL_ATOMICOPS_TEST_UNINSTRUMENTED_TSAN)
  GTEST_SKIP() << "Platform atomicops backend uses inline assembly/volatile "
                  "without -DTHREAD_SANITIZER";
#else
  TestAcquireReleaseVisibility<butil::subtle::Atomic32>();
  TestAcquireReleaseVisibility<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestAcquireReleaseVisibility<butil::subtle::Atomic64>();
#endif
#endif
}

#if !defined(BUTIL_ATOMICOPS_TEST_TSAN)
TEST(AtomicOpsTest, ReleaseCompareAndSwapFailureOrdering) {
  TestReleaseCompareAndSwapFailureOrdering<butil::subtle::Atomic32>();
  TestReleaseCompareAndSwapFailureOrdering<butil::subtle::AtomicWord>();
#ifdef ARCH_CPU_64_BITS
  TestReleaseCompareAndSwapFailureOrdering<butil::subtle::Atomic64>();
#endif
}
#endif
