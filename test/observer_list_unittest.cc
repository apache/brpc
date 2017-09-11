// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/observer_list.h"

#include <vector>

#include "butil/compiler_specific.h"
#include "butil/memory/weak_ptr.h"
#include "butil/threading/platform_thread.h"
#include <gtest/gtest.h>

namespace butil {
namespace {

class Foo {
 public:
  virtual void Observe(int x) = 0;
  virtual ~Foo() {}
};

class Adder : public Foo {
 public:
  explicit Adder(int scaler) : total(0), scaler_(scaler) {}
  virtual void Observe(int x) OVERRIDE {
    total += x * scaler_;
  }
  virtual ~Adder() {}
  int total;

 private:
  int scaler_;
};

class Disrupter : public Foo {
 public:
  Disrupter(ObserverList<Foo>* list, Foo* doomed)
      : list_(list),
        doomed_(doomed) {
  }
  virtual ~Disrupter() {}
  virtual void Observe(int x) OVERRIDE {
    list_->RemoveObserver(doomed_);
  }

 private:
  ObserverList<Foo>* list_;
  Foo* doomed_;
};

template <typename ObserverListType>
class AddInObserve : public Foo {
 public:
  explicit AddInObserve(ObserverListType* observer_list)
      : added(false),
        observer_list(observer_list),
        adder(1) {
  }

  virtual void Observe(int x) OVERRIDE {
    if (!added) {
      added = true;
      observer_list->AddObserver(&adder);
    }
  }

  bool added;
  ObserverListType* observer_list;
  Adder adder;
};

TEST(ObserverListTest, BasicTest) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1), e(-1);
  Disrupter evil(&observer_list, &c);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  FOR_EACH_OBSERVER(Foo, observer_list, Observe(10));

  observer_list.AddObserver(&evil);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  // Removing an observer not in the list should do nothing.
  observer_list.RemoveObserver(&e);

  FOR_EACH_OBSERVER(Foo, observer_list, Observe(10));

  EXPECT_EQ(20, a.total);
  EXPECT_EQ(-20, b.total);
  EXPECT_EQ(0, c.total);
  EXPECT_EQ(-10, d.total);
  EXPECT_EQ(0, e.total);
}

TEST(ObserverListTest, Existing) {
  ObserverList<Foo> observer_list(ObserverList<Foo>::NOTIFY_EXISTING_ONLY);
  Adder a(1);
  AddInObserve<ObserverList<Foo> > b(&observer_list);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  FOR_EACH_OBSERVER(Foo, observer_list, Observe(1));

  EXPECT_TRUE(b.added);
  // B's adder should not have been notified because it was added during
  // notification.
  EXPECT_EQ(0, b.adder.total);

  // Notify again to make sure b's adder is notified.
  FOR_EACH_OBSERVER(Foo, observer_list, Observe(1));
  EXPECT_EQ(1, b.adder.total);
}

class AddInClearObserve : public Foo {
 public:
  explicit AddInClearObserve(ObserverList<Foo>* list)
      : list_(list), added_(false), adder_(1) {}

  virtual void Observe(int /* x */) OVERRIDE {
    list_->Clear();
    list_->AddObserver(&adder_);
    added_ = true;
  }

  bool added() const { return added_; }
  const Adder& adder() const { return adder_; }

 private:
  ObserverList<Foo>* const list_;

  bool added_;
  Adder adder_;
};

TEST(ObserverListTest, ClearNotifyAll) {
  ObserverList<Foo> observer_list;
  AddInClearObserve a(&observer_list);

  observer_list.AddObserver(&a);

  FOR_EACH_OBSERVER(Foo, observer_list, Observe(1));
  EXPECT_TRUE(a.added());
  EXPECT_EQ(1, a.adder().total)
      << "Adder should observe once and have sum of 1.";
}

TEST(ObserverListTest, ClearNotifyExistingOnly) {
  ObserverList<Foo> observer_list(ObserverList<Foo>::NOTIFY_EXISTING_ONLY);
  AddInClearObserve a(&observer_list);

  observer_list.AddObserver(&a);

  FOR_EACH_OBSERVER(Foo, observer_list, Observe(1));
  EXPECT_TRUE(a.added());
  EXPECT_EQ(0, a.adder().total)
      << "Adder should not observe, so sum should still be 0.";
}

class ListDestructor : public Foo {
 public:
  explicit ListDestructor(ObserverList<Foo>* list) : list_(list) {}
  virtual ~ListDestructor() {}

  virtual void Observe(int x) OVERRIDE {
    delete list_;
  }

 private:
  ObserverList<Foo>* list_;
};


TEST(ObserverListTest, IteratorOutlivesList) {
  ObserverList<Foo>* observer_list = new ObserverList<Foo>;
  ListDestructor a(observer_list);
  observer_list->AddObserver(&a);

  FOR_EACH_OBSERVER(Foo, *observer_list, Observe(0));
  // If this test fails, there'll be Valgrind errors when this function goes out
  // of scope.
}

}  // namespace
}  // namespace butil
