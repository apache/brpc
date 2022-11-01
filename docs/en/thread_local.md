This page explains the problems that may be caused by using pthread-local under bthread. For the usage of bthread-local, please refer to [here](server.md#bthread-local).

# thread-local problem

After calling the blocking bthread function, the pthread is likely to change, which makes [pthread_getspecific](http://linux.die.net/man/3/pthread_getspecific), [gcc __thread](https://gcc.gnu. org/onlinedocs/gcc-4.2.4/gcc/Thread_002dLocal.html) and c++11
The values ​​of thread_local variables, pthread_self(), etc. have changed, and the behavior of the following code is unpredictable:

```
thread_local SomeObject obj;
...
SomeObject* p = &obj;
p->bar();
bthread_usleep(1000);
p->bar();
```

After bthread_usleep, the bthread is likely to be in a different pthread. At this time, p points to the thread_local variable of the previous pthread, and the result of continuing to access p is unpredictable. This usage pattern often occurs when users use thread-level variables to pass business variables. In order to prevent this, one should keep in mind:

-Do not use thread-level variables to pass business data. This is a bad design pattern, and functions that rely on thread-level data are also difficult to test individually. Determine whether it is abused: If thread-level variables are not used, can business logic still run normally? Thread-level variables should only be used as optimization means, and should not directly or indirectly call any bthread functions that may block during use. For example, tcmalloc using thread-level variables will not conflict with bthread.
-If you must use thread-level variables (in business), use bthread_key_create and bthread_getspecific.

# errno problem under gcc4

gcc4 will optimize the function of [marked as __attribute__((__const__))](https://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html#index-g_t_0040code_007bconst_007d-function-attribute-2958), this mark Roughly refers to the output will not change as long as the parameters remain the same. So when the same parameter appears multiple times in a function, gcc4 will be merged into one. For example, in our system, errno is a macro whose content is *__errno_location(). The signature of this function is:

```
/* Function to get address of global `errno' variable. */
extern int *__errno_location (void) __THROW __attribute__ ((__const__));
```

Since this function is marked as `__const__` and has no parameters, when you call errno multiple times in a function, __errno_location() may be called only the first time, and then only the returned `int*` is accessed. There is no problem in pthread, because the returned `int*` is thread-local, and it will not change in a given pthread. But in bthread, this is not true, because a bthread is likely to go to another pthread after calling some functions. If gcc4 makes similar optimizations, that is, all errno in a function are replaced by the first call return Int*, the middle bthread switches to pthread, then the errno of the previous pthread may be accessed, resulting in undefined behavior.

For example, the following is a usage scenario of errno:

```
Use errno ... (original pthread)
bthread functions that may switch to another pthread.
Use errno ... (another pthread) 
```

The behavior we expect to see:

```
Use *__errno_location() ...-the thread-local errno of original pthread
bthread may switch another pthread ...
Use *__errno_location() ...-the thread-local errno of another pthread
```

Actual behavior when using gcc4:

```
int* p = __errno_location();
Use *p ...-the thread-local errno of original pthread
bthread context switches ...
Use *p ...-still the errno of original pthread, undefined behavior!!
```

Strictly speaking, this problem is not caused by gcc4, but the signature of glibc to __errno_location is not accurate enough. A function that returns a thread-local pointer depends on the segment register (the general implementation of TLS). How can this be considered const? Since we have not yet found a way to overwrite __errno_location, the current actual solution to this problem is:

**Be sure to add `-D__const__=` to the gcc compilation options of projects that directly or indirectly use bthread, that is, define `__const__` as empty to avoid gcc4 related optimizations. **

Defining `__const__` as null has almost zero effect on other parts of the program. In addition, if you do not **directly** use errno (that is, errno does not appear in your project), or use gcc
3.4. Even if `-D__const__=` is not defined, the correctness of the program will not be affected, but in order to prevent possible future problems, we strongly recommend adding it.

It should be noted that, similar to errno, pthread_self has similar problems, but generally pthread_self has no other purpose except logging, and has a small impact. After `-D__const__=`, pthread_self will also be normal.