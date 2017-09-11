#include <gtest/gtest.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <bthread/processor.h>

namespace {
volatile bool stop = false;

void* spinner(void*) {
    long counter = 0;
    for (; !stop; ++counter) {
        cpu_relax();
    }
    printf("spinned %ld\n", counter);
    return NULL;
}

void* yielder(void*) {
    int counter = 0;
    for (; !stop; ++counter) {
        sched_yield();
    }
    printf("sched_yield %d\n", counter);
    return NULL;
}

TEST(SchedYieldTest, sched_yield_when_all_core_busy) {
    stop = false;
    const int kNumCores = sysconf(_SC_NPROCESSORS_ONLN);
    ASSERT_TRUE(kNumCores > 0);
    pthread_t th0;
    pthread_create(&th0, NULL, yielder, NULL);
    
    pthread_t th[kNumCores];
    for (int i = 0; i < kNumCores; ++i) {
        pthread_create(&th[i], NULL, spinner, NULL);
    }
    sleep(1);
    stop = true;
    for (int i = 0; i < kNumCores; ++i) {
        pthread_join(th[i], NULL);
    }
    pthread_join(th0, NULL);
}
} // namespace
