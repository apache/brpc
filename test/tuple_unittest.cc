// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/tuple.h"

#include "base/compiler_specific.h"
#include <gtest/gtest.h>

namespace {

void DoAdd(int a, int b, int c, int* res) {
  *res = a + b + c;
}

struct Addy {
  Addy() { }
  void DoAdd(int a, int b, int c, int d, int* res) {
    *res = a + b + c + d;
  }
};

struct Addz {
  Addz() { }
  void DoAdd(int a, int b, int c, int d, int e, int* res) {
    *res = a + b + c + d + e;
  }
};

}  // namespace

TEST(TupleTest, Basic) {
  Tuple0 t0 ALLOW_UNUSED = MakeTuple();
  Tuple1<int> t1(1);
  Tuple2<int, const char*> t2 = MakeTuple(1, static_cast<const char*>("wee"));
  Tuple3<int, int, int> t3(1, 2, 3);
  Tuple4<int, int, int, int*> t4(1, 2, 3, &t1.a);
  Tuple5<int, int, int, int, int*> t5(1, 2, 3, 4, &t4.a);
  Tuple6<int, int, int, int, int, int*> t6(1, 2, 3, 4, 5, &t4.a);

  EXPECT_EQ(1, t1.a);
  EXPECT_EQ(1, t2.a);
  EXPECT_EQ(1, t3.a);
  EXPECT_EQ(2, t3.b);
  EXPECT_EQ(3, t3.c);
  EXPECT_EQ(1, t4.a);
  EXPECT_EQ(2, t4.b);
  EXPECT_EQ(3, t4.c);
  EXPECT_EQ(1, t5.a);
  EXPECT_EQ(2, t5.b);
  EXPECT_EQ(3, t5.c);
  EXPECT_EQ(4, t5.d);
  EXPECT_EQ(1, t6.a);
  EXPECT_EQ(2, t6.b);
  EXPECT_EQ(3, t6.c);
  EXPECT_EQ(4, t6.d);
  EXPECT_EQ(5, t6.e);

  EXPECT_EQ(1, t1.a);
  DispatchToFunction(&DoAdd, t4);
  EXPECT_EQ(6, t1.a);

  int res = 0;
  DispatchToFunction(&DoAdd, MakeTuple(9, 8, 7, &res));
  EXPECT_EQ(24, res);

  Addy addy;
  EXPECT_EQ(1, t4.a);
  DispatchToMethod(&addy, &Addy::DoAdd, t5);
  EXPECT_EQ(10, t4.a);

  Addz addz;
  EXPECT_EQ(10, t4.a);
  DispatchToMethod(&addz, &Addz::DoAdd, t6);
  EXPECT_EQ(15, t4.a);
}

namespace {

struct CopyLogger {
  CopyLogger() { ++TimesConstructed; }
  CopyLogger(const CopyLogger& tocopy) { ++TimesConstructed; ++TimesCopied; }
  ~CopyLogger() { }

  static int TimesCopied;
  static int TimesConstructed;
};

void SomeLoggerMethRef(const CopyLogger& logy, const CopyLogger* ptr, bool* b) {
  *b = &logy == ptr;
}

void SomeLoggerMethCopy(CopyLogger logy, const CopyLogger* ptr, bool* b) {
  *b = &logy == ptr;
}

int CopyLogger::TimesCopied = 0;
int CopyLogger::TimesConstructed = 0;

}  // namespace

TEST(TupleTest, Copying) {
  CopyLogger logger;
  EXPECT_EQ(0, CopyLogger::TimesCopied);
  EXPECT_EQ(1, CopyLogger::TimesConstructed);

  bool res = false;

  // Creating the tuple should copy the class to store internally in the tuple.
  Tuple3<CopyLogger, CopyLogger*, bool*> tuple(logger, &logger, &res);
  tuple.b = &tuple.a;
  EXPECT_EQ(2, CopyLogger::TimesConstructed);
  EXPECT_EQ(1, CopyLogger::TimesCopied);

  // Our internal Logger and the one passed to the function should be the same.
  res = false;
  DispatchToFunction(&SomeLoggerMethRef, tuple);
  EXPECT_TRUE(res);
  EXPECT_EQ(2, CopyLogger::TimesConstructed);
  EXPECT_EQ(1, CopyLogger::TimesCopied);

  // Now they should be different, since the function call will make a copy.
  res = false;
  DispatchToFunction(&SomeLoggerMethCopy, tuple);
  EXPECT_FALSE(res);
  EXPECT_EQ(3, CopyLogger::TimesConstructed);
  EXPECT_EQ(2, CopyLogger::TimesCopied);
}
