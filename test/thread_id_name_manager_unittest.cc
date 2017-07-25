// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread_id_name_manager.h"

#include "base/threading/platform_thread.h"
#include <gtest/gtest.h>

typedef testing::Test ThreadIdNameManagerTest;

namespace {

TEST_F(ThreadIdNameManagerTest, ThreadNameInterning) {
  base::ThreadIdNameManager* manager = base::ThreadIdNameManager::GetInstance();

  base::PlatformThreadId a_id = base::PlatformThread::CurrentId();
  base::PlatformThread::SetName("First Name");
  std::string version = manager->GetName(a_id);

  base::PlatformThread::SetName("New name");
  EXPECT_NE(version, manager->GetName(a_id));
  base::PlatformThread::SetName("");
}

TEST_F(ThreadIdNameManagerTest, ResettingNameKeepsCorrectInternedValue) {
  base::ThreadIdNameManager* manager = base::ThreadIdNameManager::GetInstance();

  base::PlatformThreadId a_id = base::PlatformThread::CurrentId();
  base::PlatformThread::SetName("Test Name");
  std::string version = manager->GetName(a_id);

  base::PlatformThread::SetName("New name");
  EXPECT_NE(version, manager->GetName(a_id));

  base::PlatformThread::SetName("Test Name");
  EXPECT_EQ(version, manager->GetName(a_id));

  base::PlatformThread::SetName("");
}

}  // namespace
