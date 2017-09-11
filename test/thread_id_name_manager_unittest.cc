// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/threading/thread_id_name_manager.h"

#include "butil/threading/platform_thread.h"
#include <gtest/gtest.h>

typedef testing::Test ThreadIdNameManagerTest;

namespace {

TEST_F(ThreadIdNameManagerTest, ThreadNameInterning) {
  butil::ThreadIdNameManager* manager = butil::ThreadIdNameManager::GetInstance();

  butil::PlatformThreadId a_id = butil::PlatformThread::CurrentId();
  butil::PlatformThread::SetName("First Name");
  std::string version = manager->GetName(a_id);

  butil::PlatformThread::SetName("New name");
  EXPECT_NE(version, manager->GetName(a_id));
  butil::PlatformThread::SetName("");
}

TEST_F(ThreadIdNameManagerTest, ResettingNameKeepsCorrectInternedValue) {
  butil::ThreadIdNameManager* manager = butil::ThreadIdNameManager::GetInstance();

  butil::PlatformThreadId a_id = butil::PlatformThread::CurrentId();
  butil::PlatformThread::SetName("Test Name");
  std::string version = manager->GetName(a_id);

  butil::PlatformThread::SetName("New name");
  EXPECT_NE(version, manager->GetName(a_id));

  butil::PlatformThread::SetName("Test Name");
  EXPECT_EQ(version, manager->GetName(a_id));

  butil::PlatformThread::SetName("");
}

}  // namespace
