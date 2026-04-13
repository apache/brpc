#ifndef BRPC_TIMER_MGR_H
#define BRPC_TIMER_MGR_H
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include "brpc/ub/common/common.h"

#define MAX_TIMER 1024
#define TIMER_EPOLL_WAIT_TIMEOUT 1000

namespace brpc {
namespace ub {
typedef enum {
    TIMER_CONTEXT_NOT_USING,
    TIMER_CONTEXT_EPOLL_WAITING,
    TIMER_CONTEXT_CALLBACK_ONGOING
} TimerFdCtxStatus;

typedef struct {
    void *(*cb)(void*);
    void *args;
    uint32_t fd;
    TimerFdCtxStatus status;
    uint32_t periodical;
    pthread_spinlock_t spinLock;
} TimerFdCtx;

RETURN_CODE TimerInit(void);
void TimerModuleDestroy(void);
void *UnifiedCallback(void *args);
void *TimerEpoll(void *args);
int32_t TimerStart(const struct itimerspec *time, void *(*cb)(void *), void *args);
uint32_t GetActiveTimerNum(void);
void CloseTimerFd(uint32_t fd);

void DeleteTimerSafe(uint32_t fd);
void DeleteTimer(uint32_t fd);
RETURN_CODE TimerFdCtxValidate(uint32_t fd);
}
}
#endif //BRPC_TIMER_MGR_H