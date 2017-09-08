// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/memory/ref_counted.h"
#include <gtest/gtest.h>

namespace {

class SelfAssign : public butil::RefCounted<SelfAssign> {
  friend class butil::RefCounted<SelfAssign>;

  ~SelfAssign() {}
};

class CheckDerivedMemberAccess : public scoped_refptr<SelfAssign> {
 public:
  CheckDerivedMemberAccess() {
    // This shouldn't compile if we don't have access to the member variable.
    SelfAssign** pptr = &ptr_;
    EXPECT_EQ(*pptr, ptr_);
  }
};

class ScopedRefPtrToSelf : public butil::RefCounted<ScopedRefPtrToSelf> {
 public:
  ScopedRefPtrToSelf() : self_ptr_(this) {}

  static bool was_destroyed() { return was_destroyed_; }

  void SelfDestruct() { self_ptr_ = NULL; }

 private:
  friend class butil::RefCounted<ScopedRefPtrToSelf>;
  ~ScopedRefPtrToSelf() { was_destroyed_ = true; }

  static bool was_destroyed_;

  scoped_refptr<ScopedRefPtrToSelf> self_ptr_;
};

bool ScopedRefPtrToSelf::was_destroyed_ = false;

}  // end namespace

TEST(RefCountedUnitTest, TestSelfAssignment) {
  SelfAssign* p = new SelfAssign;
  scoped_refptr<SelfAssign> var(p);
  var = var;
  EXPECT_EQ(var.get(), p);
}

TEST(RefCountedUnitTest, ScopedRefPtrMemberAccess) {
  CheckDerivedMemberAccess check;
}

TEST(RefCountedUnitTest, ScopedRefPtrToSelf) {
  ScopedRefPtrToSelf* check = new ScopedRefPtrToSelf();
  EXPECT_FALSE(ScopedRefPtrToSelf::was_destroyed());
  check->SelfDestruct();
  EXPECT_TRUE(ScopedRefPtrToSelf::was_destroyed());
}

TEST(RefCountedUnitTest, ScopedRefPtrBooleanOperations) {
  scoped_refptr<SelfAssign> p1 = new SelfAssign;
  scoped_refptr<SelfAssign> p2;

  EXPECT_TRUE(p1);
  EXPECT_FALSE(!p1);

  EXPECT_TRUE(!p2);
  EXPECT_FALSE(p2);

  EXPECT_NE(p1, p2);

  SelfAssign* raw_p = new SelfAssign;
  p2 = raw_p;
  EXPECT_NE(p1, p2);
  EXPECT_EQ(raw_p, p2);

  p2 = p1;
  EXPECT_NE(raw_p, p2);
  EXPECT_EQ(p1, p2);
}
