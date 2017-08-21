// libcontext - a slightly more portable version of boost::context
// Copyright Martin Husemann 2013.
// Copyright Oliver Kowalke 2009.
// Copyright Sergue E. Leontiev 2013.
// Copyright Thomas Sailer 2013.
// Minor modifications by Tomasz Wlostowski 2016.
// Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#ifndef BTHREAD_CONTEXT_H
#define BTHREAD_CONTEXT_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#if defined(__GNUC__) || defined(__APPLE__)

    #define BTHREAD_CONTEXT_COMPILER_gcc

    #if defined(__linux__)
	#ifdef __x86_64__
	    #define BTHREAD_CONTEXT_PLATFORM_linux_x86_64
	    #define BTHREAD_CONTEXT_CALL_CONVENTION

	#elif __i386__
	    #define BTHREAD_CONTEXT_PLATFORM_linux_i386
	    #define BTHREAD_CONTEXT_CALL_CONVENTION
	#elif __arm__
	    #define BTHREAD_CONTEXT_PLATFORM_linux_arm32
	    #define BTHREAD_CONTEXT_CALL_CONVENTION
	#endif

    #elif defined(__MINGW32__) || defined (__MINGW64__)
	#if defined(__x86_64__)
	    #define BTHREAD_CONTEXT_COMPILER_gcc
    	    #define BTHREAD_CONTEXT_PLATFORM_windows_x86_64
	    #define BTHREAD_CONTEXT_CALL_CONVENTION
	#endif

	#if defined(__i386__)
	    #define BTHREAD_CONTEXT_COMPILER_gcc
	    #define BTHREAD_CONTEXT_PLATFORM_windows_i386
	    #define BTHREAD_CONTEXT_CALL_CONVENTION __cdecl
	#endif
    #elif defined(__APPLE__) && defined(__MACH__)
	#if defined (__i386__)
	    #define BTHREAD_CONTEXT_PLATFORM_apple_i386
	    #define BTHREAD_CONTEXT_CALL_CONVENTION
	#elif defined (__x86_64__)
	    #define BTHREAD_CONTEXT_PLATFORM_apple_x86_64
	    #define BTHREAD_CONTEXT_CALL_CONVENTION
	#endif
    #endif
#endif

#if defined(_WIN32_WCE)
typedef int intptr_t;
#endif

typedef void* bthread_fcontext_t;

#ifdef __cplusplus
extern "C"{
#endif

intptr_t BTHREAD_CONTEXT_CALL_CONVENTION
bthread_jump_fcontext(bthread_fcontext_t * ofc, bthread_fcontext_t nfc,
                      intptr_t vp, bool preserve_fpu = false);
bthread_fcontext_t BTHREAD_CONTEXT_CALL_CONVENTION
bthread_make_fcontext(void* sp, size_t size, void (* fn)( intptr_t));

#ifdef __cplusplus
};
#endif

#endif  // BTHREAD_CONTEXT_H
