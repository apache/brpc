#ifndef BUTIL_COMPAT_H
#define BUTIL_COMPAT_H

// TODO: Some functions in this header are not implemented yet.

#include "butil/build_config.h"
#include <pthread.h>

#if defined(OS_MACOSX)

#include <sys/cdefs.h>
#include <stdint.h>
#include <dispatch/dispatch.h>    // dispatch_semaphore
#include <errno.h>                // EINVAL

__BEGIN_DECLS

// Implement pthread_spinlock_t for MAC.
struct pthread_spinlock_t {
    dispatch_semaphore_t sem;
};
inline int pthread_spin_init (pthread_spinlock_t *__lock, int __pshared) {
    if (__pshared != 0) {
        return EINVAL;
    }
    __lock->sem = dispatch_semaphore_create(1);
    return 0;
}
inline int pthread_spin_destroy (pthread_spinlock_t *__lock) {
    // TODO(gejun): Not see any destructive API on dispatch_semaphore
    (void)__lock;
    return 0;
}
inline int pthread_spin_lock (pthread_spinlock_t *__lock) {
    return (int)dispatch_semaphore_wait(__lock->sem, DISPATCH_TIME_FOREVER);
}
inline int pthread_spin_trylock (pthread_spinlock_t *__lock) {
    return dispatch_semaphore_wait(__lock->sem, DISPATCH_TIME_NOW) == 0;
}
inline int pthread_spin_unlock (pthread_spinlock_t *__lock) {
    return dispatch_semaphore_signal(__lock->sem);
}

__END_DECLS

// fake epoll (temporary)
enum EPOLL_EVENTS
{
    EPOLLIN = 0x001,
#define EPOLLIN EPOLLIN
    EPOLLPRI = 0x002,
#define EPOLLPRI EPOLLPRI
    EPOLLOUT = 0x004,
#define EPOLLOUT EPOLLOUT
    EPOLLRDNORM = 0x040,
#define EPOLLRDNORM EPOLLRDNORM
    EPOLLRDBAND = 0x080,
#define EPOLLRDBAND EPOLLRDBAND
    EPOLLWRNORM = 0x100,
#define EPOLLWRNORM EPOLLWRNORM
    EPOLLWRBAND = 0x200,
#define EPOLLWRBAND EPOLLWRBAND
    EPOLLMSG = 0x400,
#define EPOLLMSG EPOLLMSG
    EPOLLERR = 0x008,
#define EPOLLERR EPOLLERR
    EPOLLHUP = 0x010,
#define EPOLLHUP EPOLLHUP
    EPOLLRDHUP = 0x2000,
#define EPOLLRDHUP EPOLLRDHUP
    EPOLLONESHOT = (1 << 30),
#define EPOLLONESHOT EPOLLONESHOT
    EPOLLET = (1 << 31)
#define EPOLLET EPOLLET
};

/* Valid opcodes ( "op" parameter ) to issue to epoll_ctl().  */
#define EPOLL_CTL_ADD 1 /* Add a file decriptor to the interface.  */
#define EPOLL_CTL_DEL 2 /* Remove a file decriptor from the interface.  */
#define EPOLL_CTL_MOD 3 /* Change file decriptor epoll_event structure.  */

typedef union epoll_data
{
  void *ptr;
  int fd;
  uint32_t u32;
  uint64_t u64;
} epoll_data_t;

struct epoll_event
{
  uint32_t events;  /* Epoll events */
  epoll_data_t data;    /* User data variable */
} __attribute__ ((__packed__));


__BEGIN_DECLS

/* Creates an epoll instance.  Returns an fd for the new instance.
   The "size" parameter is a hint specifying the number of file
   descriptors to be associated with the new instance.  The fd
   returned by epoll_create() should be closed with close().  */
extern int epoll_create (int __size);

/* Same as epoll_create but with an FLAGS parameter.  The unused SIZE
   parameter has been dropped.  */
extern int epoll_create1 (int __flags);


/* Manipulate an epoll instance "epfd". Returns 0 in case of success,
   -1 in case of error ( the "errno" variable will contain the
   specific error code ) The "op" parameter is one of the EPOLL_CTL_*
   constants defined above. The "fd" parameter is the target of the
   operation. The "event" parameter describes which events the caller
   is interested in and any associated user data.  */
extern int epoll_ctl (int __epfd, int __op, int __fd,
              struct epoll_event *__event);

/* Wait for events on an epoll instance "epfd". Returns the number of
   triggered events returned in "events" buffer. Or -1 in case of
   error with the "errno" variable set to the specific error code. The
   "events" parameter is a buffer that will contain triggered
   events. The "maxevents" is the maximum number of events to be
   returned ( usually size of "events" ). The "timeout" parameter
   specifies the maximum wait time in milliseconds (-1 == infinite).

   This function is a cancellation point and therefore not marked with
  .  */
extern int epoll_wait (int __epfd, struct epoll_event *__events,
               int __maxevents, int __timeout);

__END_DECLS

#elif defined(OS_LINUX)

#include <sys/epoll.h>

#else

#error "The platform does not support epoll-like APIs"

#endif // defined(OS_MACOSX)

__BEGIN_DECLS

inline uint64_t pthread_numeric_id() {
#if defined(OS_MACOSX)
    uint64_t id;
    if (pthread_threadid_np(pthread_self(), &id) == 0) {
        return id;
    }
    return -1;
#else
    return pthread_self();
#endif
}

__END_DECLS

#endif // BUTIL_COMPAT_H
