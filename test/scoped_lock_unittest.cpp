// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010-12-04 11:59

#include <gtest/gtest.h>
#include "butil/scoped_lock.h"
#include <errno.h>

namespace {
class ScopedLockTest : public ::testing::Test{
protected:
    ScopedLockTest(){
    };
    virtual ~ScopedLockTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

TEST_F(ScopedLockTest, mutex) {    
    pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
    {
        BAIDU_SCOPED_LOCK(m1);
        ASSERT_EQ(EBUSY, pthread_mutex_trylock(&m1));
    }
    ASSERT_EQ(0, pthread_mutex_trylock(&m1));
    pthread_mutex_unlock(&m1);
}

TEST_F(ScopedLockTest, spinlock) {    
    pthread_spinlock_t s1;
    pthread_spin_init(&s1, 0);
    {
        BAIDU_SCOPED_LOCK(s1);
        ASSERT_EQ(EBUSY, pthread_spin_trylock(&s1));
    }
    ASSERT_EQ(0, pthread_spin_lock(&s1));
    pthread_spin_unlock(&s1);
    pthread_spin_destroy(&s1);
}

TEST_F(ScopedLockTest, unique_lock_mutex) {
    pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
    {
        std::unique_lock<pthread_mutex_t> lck(m1);
        ASSERT_EQ(EBUSY, pthread_mutex_trylock(&m1));
        lck.unlock();
        {
            std::unique_lock<pthread_mutex_t> lck2(m1, std::try_to_lock);
            ASSERT_TRUE(lck2.owns_lock());
        }
        ASSERT_TRUE(lck.try_lock());
        ASSERT_TRUE(lck.owns_lock());
        std::unique_lock<pthread_mutex_t> lck2(m1, std::defer_lock);
        ASSERT_FALSE(lck2.owns_lock());
        std::unique_lock<pthread_mutex_t> lck3(m1, std::try_to_lock);
        ASSERT_FALSE(lck3.owns_lock());
    }
    {
        BAIDU_SCOPED_LOCK(m1);
        ASSERT_EQ(EBUSY, pthread_mutex_trylock(&m1));
    }
    ASSERT_EQ(0, pthread_mutex_trylock(&m1));
    {
        std::unique_lock<pthread_mutex_t> lck(m1, std::adopt_lock);
        ASSERT_TRUE(lck.owns_lock());
    }
    std::unique_lock<pthread_mutex_t> lck(m1, std::try_to_lock);
    ASSERT_TRUE(lck.owns_lock());
}

TEST_F(ScopedLockTest, unique_lock_spin) {
    pthread_spinlock_t s1;
    pthread_spin_init(&s1, 0);
    {
        std::unique_lock<pthread_spinlock_t> lck(s1);
        ASSERT_EQ(EBUSY, pthread_spin_trylock(&s1));
        lck.unlock();
        ASSERT_TRUE(lck.try_lock());
    }
    {
        BAIDU_SCOPED_LOCK(s1);
        ASSERT_EQ(EBUSY, pthread_spin_trylock(&s1));
    }
    ASSERT_EQ(0, pthread_spin_lock(&s1));
    pthread_spin_unlock(&s1);
    pthread_spin_destroy(&s1);
}

}
