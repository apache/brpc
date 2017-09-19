// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/memory/scoped_ptr.h"

#include "butil/basictypes.h"
#include <gtest/gtest.h>

namespace {

// Used to test depth subtyping.
class ConDecLoggerParent {
 public:
  virtual ~ConDecLoggerParent() {}

  virtual void SetPtr(int* ptr) = 0;

  virtual int SomeMeth(int x) const = 0;
};

class ConDecLogger : public ConDecLoggerParent {
 public:
  ConDecLogger() : ptr_(NULL) { }
  explicit ConDecLogger(int* ptr) { SetPtr(ptr); }
  virtual ~ConDecLogger() { --*ptr_; }

  virtual void SetPtr(int* ptr) OVERRIDE { ptr_ = ptr; ++*ptr_; }

  virtual int SomeMeth(int x) const OVERRIDE { return x; }

 private:
  int* ptr_;

  DISALLOW_COPY_AND_ASSIGN(ConDecLogger);
};

struct CountingDeleter {
  explicit CountingDeleter(int* count) : count_(count) {}
  inline void operator()(double* ptr) const {
    (*count_)++;
  }
  int* count_;
};

// Used to test assignment of convertible deleters.
struct CountingDeleterChild : public CountingDeleter {
  explicit CountingDeleterChild(int* count) : CountingDeleter(count) {}
};

class OverloadedNewAndDelete {
 public:
  void* operator new(size_t size) {
    g_new_count++;
    return malloc(size);
  }

  void operator delete(void* ptr) {
    g_delete_count++;
    free(ptr);
  }

  static void ResetCounters() {
    g_new_count = 0;
    g_delete_count = 0;
  }

  static int new_count() { return g_new_count; }
  static int delete_count() { return g_delete_count; }

 private:
  static int g_new_count;
  static int g_delete_count;
};

int OverloadedNewAndDelete::g_new_count = 0;
int OverloadedNewAndDelete::g_delete_count = 0;

scoped_ptr<ConDecLogger> PassThru(scoped_ptr<ConDecLogger> logger) {
  return logger.Pass();
}

void GrabAndDrop(scoped_ptr<ConDecLogger> logger) {
}

// Do not delete this function!  It's existence is to test that you can
// return a temporarily constructed version of the scoper.
scoped_ptr<ConDecLogger> TestReturnOfType(int* constructed) {
  return scoped_ptr<ConDecLogger>(new ConDecLogger(constructed));
}

scoped_ptr<ConDecLoggerParent> UpcastUsingPassAs(
    scoped_ptr<ConDecLogger> object) {
  return object.PassAs<ConDecLoggerParent>();
}

}  // namespace

TEST(ScopedPtrTest, ScopedPtr) {
  int constructed = 0;

  // Ensure size of scoped_ptr<> doesn't increase unexpectedly.
  COMPILE_ASSERT(sizeof(int*) >= sizeof(scoped_ptr<int>),
                 scoped_ptr_larger_than_raw_ptr);

  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    EXPECT_EQ(10, scoper->SomeMeth(10));
    EXPECT_EQ(10, scoper.get()->SomeMeth(10));
    EXPECT_EQ(10, (*scoper).SomeMeth(10));
  }
  EXPECT_EQ(0, constructed);

  // Test reset() and release()
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoper.reset(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoper.reset();
    EXPECT_EQ(0, constructed);
    EXPECT_FALSE(scoper.get());

    scoper.reset(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    ConDecLogger* take = scoper.release();
    EXPECT_EQ(1, constructed);
    EXPECT_FALSE(scoper.get());
    delete take;
    EXPECT_EQ(0, constructed);

    scoper.reset(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Test swap(), == and !=
  {
    scoped_ptr<ConDecLogger> scoper1;
    scoped_ptr<ConDecLogger> scoper2;
    EXPECT_TRUE(scoper1 == scoper2.get());
    EXPECT_FALSE(scoper1 != scoper2.get());

    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoper1.reset(logger);
    EXPECT_EQ(logger, scoper1.get());
    EXPECT_FALSE(scoper2.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());

    scoper2.swap(scoper1);
    EXPECT_EQ(logger, scoper2.get());
    EXPECT_FALSE(scoper1.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());
  }
  EXPECT_EQ(0, constructed);
}

TEST(ScopedPtrTest, ScopedPtrDepthSubtyping) {
  int constructed = 0;

  // Test construction from a scoped_ptr to a derived class.
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoped_ptr<ConDecLoggerParent> scoper_parent(scoper.Pass());
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper_parent.get());
    EXPECT_FALSE(scoper.get());

    EXPECT_EQ(10, scoper_parent->SomeMeth(10));
    EXPECT_EQ(10, scoper_parent.get()->SomeMeth(10));
    EXPECT_EQ(10, (*scoper_parent).SomeMeth(10));
  }
  EXPECT_EQ(0, constructed);

  // Test assignment from a scoped_ptr to a derived class.
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoped_ptr<ConDecLoggerParent> scoper_parent;
    scoper_parent = scoper.Pass();
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper_parent.get());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Test construction of a scoped_ptr with an additional const annotation.
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoped_ptr<const ConDecLogger> scoper_const(scoper.Pass());
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper_const.get());
    EXPECT_FALSE(scoper.get());

    EXPECT_EQ(10, scoper_const->SomeMeth(10));
    EXPECT_EQ(10, scoper_const.get()->SomeMeth(10));
    EXPECT_EQ(10, (*scoper_const).SomeMeth(10));
  }
  EXPECT_EQ(0, constructed);

  // Test assignment to a scoped_ptr with an additional const annotation.
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoped_ptr<const ConDecLogger> scoper_const;
    scoper_const = scoper.Pass();
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper_const.get());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Test assignment to a scoped_ptr deleter of parent type.
  {
    // Custom deleters never touch these value.
    double dummy_value, dummy_value2;
    int deletes = 0;
    int alternate_deletes = 0;
    scoped_ptr<double, CountingDeleter> scoper(&dummy_value,
                                               CountingDeleter(&deletes));
    scoped_ptr<double, CountingDeleterChild> scoper_child(
        &dummy_value2, CountingDeleterChild(&alternate_deletes));

    EXPECT_TRUE(scoper);
    EXPECT_TRUE(scoper_child);
    EXPECT_EQ(0, deletes);
    EXPECT_EQ(0, alternate_deletes);

    // Test this compiles and correctly overwrites the deleter state.
    scoper = scoper_child.Pass();
    EXPECT_TRUE(scoper);
    EXPECT_FALSE(scoper_child);
    EXPECT_EQ(1, deletes);
    EXPECT_EQ(0, alternate_deletes);

    scoper.reset();
    EXPECT_FALSE(scoper);
    EXPECT_FALSE(scoper_child);
    EXPECT_EQ(1, deletes);
    EXPECT_EQ(1, alternate_deletes);

    scoper_child.reset(&dummy_value);
    EXPECT_TRUE(scoper_child);
    EXPECT_EQ(1, deletes);
    EXPECT_EQ(1, alternate_deletes);
    scoped_ptr<double, CountingDeleter> scoper_construct(scoper_child.Pass());
    EXPECT_TRUE(scoper_construct);
    EXPECT_FALSE(scoper_child);
    EXPECT_EQ(1, deletes);
    EXPECT_EQ(1, alternate_deletes);

    scoper_construct.reset();
    EXPECT_EQ(1, deletes);
    EXPECT_EQ(2, alternate_deletes);
  }
}

TEST(ScopedPtrTest, ScopedPtrWithArray) {
  static const int kNumLoggers = 12;

  int constructed = 0;

  {
    scoped_ptr<ConDecLogger[]> scoper(new ConDecLogger[kNumLoggers]);
    EXPECT_TRUE(scoper);
    EXPECT_EQ(&scoper[0], scoper.get());
    for (int i = 0; i < kNumLoggers; ++i) {
      scoper[i].SetPtr(&constructed);
    }
    EXPECT_EQ(12, constructed);

    EXPECT_EQ(10, scoper.get()->SomeMeth(10));
    EXPECT_EQ(10, scoper[2].SomeMeth(10));
  }
  EXPECT_EQ(0, constructed);

  // Test reset() and release()
  {
    scoped_ptr<ConDecLogger[]> scoper;
    EXPECT_FALSE(scoper.get());
    EXPECT_FALSE(scoper.release());
    EXPECT_FALSE(scoper.get());
    scoper.reset();
    EXPECT_FALSE(scoper.get());

    scoper.reset(new ConDecLogger[kNumLoggers]);
    for (int i = 0; i < kNumLoggers; ++i) {
      scoper[i].SetPtr(&constructed);
    }
    EXPECT_EQ(12, constructed);
    scoper.reset();
    EXPECT_EQ(0, constructed);

    scoper.reset(new ConDecLogger[kNumLoggers]);
    for (int i = 0; i < kNumLoggers; ++i) {
      scoper[i].SetPtr(&constructed);
    }
    EXPECT_EQ(12, constructed);
    ConDecLogger* ptr = scoper.release();
    EXPECT_EQ(12, constructed);
    delete[] ptr;
    EXPECT_EQ(0, constructed);
  }
  EXPECT_EQ(0, constructed);

  // Test swap(), ==, !=, and type-safe Boolean.
  {
    scoped_ptr<ConDecLogger[]> scoper1;
    scoped_ptr<ConDecLogger[]> scoper2;
    EXPECT_TRUE(scoper1 == scoper2.get());
    EXPECT_FALSE(scoper1 != scoper2.get());

    ConDecLogger* loggers = new ConDecLogger[kNumLoggers];
    for (int i = 0; i < kNumLoggers; ++i) {
      loggers[i].SetPtr(&constructed);
    }
    scoper1.reset(loggers);
    EXPECT_TRUE(scoper1);
    EXPECT_EQ(loggers, scoper1.get());
    EXPECT_FALSE(scoper2);
    EXPECT_FALSE(scoper2.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());

    scoper2.swap(scoper1);
    EXPECT_EQ(loggers, scoper2.get());
    EXPECT_FALSE(scoper1.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());
  }
  EXPECT_EQ(0, constructed);

  {
    ConDecLogger* loggers = new ConDecLogger[kNumLoggers];
    scoped_ptr<ConDecLogger[]> scoper(loggers);
    EXPECT_TRUE(scoper);
    for (int i = 0; i < kNumLoggers; ++i) {
      scoper[i].SetPtr(&constructed);
    }
    EXPECT_EQ(kNumLoggers, constructed);

    // Test Pass() with constructor;
    scoped_ptr<ConDecLogger[]> scoper2(scoper.Pass());
    EXPECT_EQ(kNumLoggers, constructed);

    // Test Pass() with assignment;
    scoped_ptr<ConDecLogger[]> scoper3;
    scoper3 = scoper2.Pass();
    EXPECT_EQ(kNumLoggers, constructed);
    EXPECT_FALSE(scoper);
    EXPECT_FALSE(scoper2);
    EXPECT_TRUE(scoper3);
  }
  EXPECT_EQ(0, constructed);
}

TEST(ScopedPtrTest, PassBehavior) {
  int constructed = 0;
  {
    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoped_ptr<ConDecLogger> scoper(logger);
    EXPECT_EQ(1, constructed);

    // Test Pass() with constructor;
    scoped_ptr<ConDecLogger> scoper2(scoper.Pass());
    EXPECT_EQ(1, constructed);

    // Test Pass() with assignment;
    scoped_ptr<ConDecLogger> scoper3;
    scoper3 = scoper2.Pass();
    EXPECT_EQ(1, constructed);
    EXPECT_FALSE(scoper.get());
    EXPECT_FALSE(scoper2.get());
    EXPECT_TRUE(scoper3.get());
  }

  // Test uncaught Pass() does not leak.
  {
    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoped_ptr<ConDecLogger> scoper(logger);
    EXPECT_EQ(1, constructed);

    // Should auto-destruct logger by end of scope.
    scoper.Pass();
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Test that passing to function which does nothing does not leak.
  {
    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoped_ptr<ConDecLogger> scoper(logger);
    EXPECT_EQ(1, constructed);

    // Should auto-destruct logger by end of scope.
    GrabAndDrop(scoper.Pass());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);
}

TEST(ScopedPtrTest, ReturnTypeBehavior) {
  int constructed = 0;

  // Test that we can return a scoped_ptr.
  {
    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoped_ptr<ConDecLogger> scoper(logger);
    EXPECT_EQ(1, constructed);

    PassThru(scoper.Pass());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Test uncaught return type not leak.
  {
    ConDecLogger* logger = new ConDecLogger(&constructed);
    scoped_ptr<ConDecLogger> scoper(logger);
    EXPECT_EQ(1, constructed);

    // Should auto-destruct logger by end of scope.
    PassThru(scoper.Pass());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);

  // Call TestReturnOfType() so the compiler doesn't warn for an unused
  // function.
  {
    TestReturnOfType(&constructed);
  }
  EXPECT_EQ(0, constructed);
}

TEST(ScopedPtrTest, PassAs) {
  int constructed = 0;
  {
    scoped_ptr<ConDecLogger> scoper(new ConDecLogger(&constructed));
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper.get());

    scoped_ptr<ConDecLoggerParent> scoper_parent;
    scoper_parent = UpcastUsingPassAs(scoper.Pass());
    EXPECT_EQ(1, constructed);
    EXPECT_TRUE(scoper_parent.get());
    EXPECT_FALSE(scoper.get());
  }
  EXPECT_EQ(0, constructed);
}

TEST(ScopedPtrTest, CustomDeleter) {
  double dummy_value;  // Custom deleter never touches this value.
  int deletes = 0;
  int alternate_deletes = 0;

  // Normal delete support.
  {
    deletes = 0;
    scoped_ptr<double, CountingDeleter> scoper(&dummy_value,
                                               CountingDeleter(&deletes));
    EXPECT_EQ(0, deletes);
    EXPECT_TRUE(scoper.get());
  }
  EXPECT_EQ(1, deletes);

  // Test reset() and release().
  deletes = 0;
  {
    scoped_ptr<double, CountingDeleter> scoper(NULL,
                                               CountingDeleter(&deletes));
    EXPECT_FALSE(scoper.get());
    EXPECT_FALSE(scoper.release());
    EXPECT_FALSE(scoper.get());
    scoper.reset();
    EXPECT_FALSE(scoper.get());
    EXPECT_EQ(0, deletes);

    scoper.reset(&dummy_value);
    scoper.reset();
    EXPECT_EQ(1, deletes);

    scoper.reset(&dummy_value);
    EXPECT_EQ(&dummy_value, scoper.release());
  }
  EXPECT_EQ(1, deletes);

  // Test get_deleter().
  deletes = 0;
  alternate_deletes = 0;
  {
    scoped_ptr<double, CountingDeleter> scoper(&dummy_value,
                                               CountingDeleter(&deletes));
    // Call deleter manually.
    EXPECT_EQ(0, deletes);
    scoper.get_deleter()(&dummy_value);
    EXPECT_EQ(1, deletes);

    // Deleter is still there after reset.
    scoper.reset();
    EXPECT_EQ(2, deletes);
    scoper.get_deleter()(&dummy_value);
    EXPECT_EQ(3, deletes);

    // Deleter can be assigned into (matches C++11 unique_ptr<> spec).
    scoper.get_deleter() = CountingDeleter(&alternate_deletes);
    scoper.reset(&dummy_value);
    EXPECT_EQ(0, alternate_deletes);

  }
  EXPECT_EQ(3, deletes);
  EXPECT_EQ(1, alternate_deletes);

  // Test operator= deleter support.
  deletes = 0;
  alternate_deletes = 0;
  {
    double dummy_value2;
    scoped_ptr<double, CountingDeleter> scoper(&dummy_value,
                                               CountingDeleter(&deletes));
    scoped_ptr<double, CountingDeleter> scoper2(
        &dummy_value2,
        CountingDeleter(&alternate_deletes));
    EXPECT_EQ(0, deletes);
    EXPECT_EQ(0, alternate_deletes);

    // Pass the second deleter through a constructor and an operator=. Then
    // reinitialize the empty scopers to ensure that each one is deleting
    // properly.
    scoped_ptr<double, CountingDeleter> scoper3(scoper2.Pass());
    scoper = scoper3.Pass();
    EXPECT_EQ(1, deletes);

    scoper2.reset(&dummy_value2);
    scoper3.reset(&dummy_value2);
    EXPECT_EQ(0, alternate_deletes);

  }
  EXPECT_EQ(1, deletes);
  EXPECT_EQ(3, alternate_deletes);

  // Test swap(), ==, !=, and type-safe Boolean.
  {
    scoped_ptr<double, CountingDeleter> scoper1(NULL,
                                                CountingDeleter(&deletes));
    scoped_ptr<double, CountingDeleter> scoper2(NULL,
                                                CountingDeleter(&deletes));
    EXPECT_TRUE(scoper1 == scoper2.get());
    EXPECT_FALSE(scoper1 != scoper2.get());

    scoper1.reset(&dummy_value);
    EXPECT_TRUE(scoper1);
    EXPECT_EQ(&dummy_value, scoper1.get());
    EXPECT_FALSE(scoper2);
    EXPECT_FALSE(scoper2.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());

    scoper2.swap(scoper1);
    EXPECT_EQ(&dummy_value, scoper2.get());
    EXPECT_FALSE(scoper1.get());
    EXPECT_FALSE(scoper1 == scoper2.get());
    EXPECT_TRUE(scoper1 != scoper2.get());
  }
}

// Sanity check test for overloaded new and delete operators. Does not do full
// coverage of reset/release/Pass() operations as that is redundant with the
// above.
TEST(ScopedPtrTest, OverloadedNewAndDelete) {
  {
    OverloadedNewAndDelete::ResetCounters();
    scoped_ptr<OverloadedNewAndDelete> scoper(new OverloadedNewAndDelete());
    EXPECT_TRUE(scoper.get());

    scoped_ptr<OverloadedNewAndDelete> scoper2(scoper.Pass());
  }
  EXPECT_EQ(1, OverloadedNewAndDelete::delete_count());
  EXPECT_EQ(1, OverloadedNewAndDelete::new_count());
}
