/* Copyright (c) 2005, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Sanjay Ghemawat
 *
 * Module for CPU profiling based on periodic pc-sampling.
 *
 * These functions are thread-safe.
 */

#ifndef BUTIL_GPERFTOOLS_PROFILER_H_
#define BUTIL_GPERFTOOLS_PROFILER_H_

/* Annoying stuff for windows; makes sure clients can import these functions */
#ifndef BRPC_DLL_DECL
# ifdef _WIN32
#   define BRPC_DLL_DECL  __declspec(dllimport)
# else
#   define BRPC_DLL_DECL
# endif
#endif

/* All this code should be usable from within C apps. */
#ifdef __cplusplus
extern "C" {
#endif

/* Start profiling and write profile info into fname, discarding any
 * existing profiling data in that file.
 *
 * This is equivalent to calling ProfilerStartWithOptions(fname, NULL).
 */
BRPC_DLL_DECL int ProfilerStart(const char* fname);

/* Stop profiling. Can be started again with ProfilerStart(), but
 * the currently accumulated profiling data will be cleared.
 */
BRPC_DLL_DECL void ProfilerStop();

/* Flush any currently buffered profiling state to the profile file.
 * Has no effect if the profiler has not been started.
 */
BRPC_DLL_DECL void ProfilerFlush();

/* Returns nonzero if profile is currently enabled, zero if it's not. */
BRPC_DLL_DECL int ProfilingIsEnabledForAllThreads();

/* Routine for registering new threads with the profiler.
 */
BRPC_DLL_DECL void ProfilerRegisterThread();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  /* BUTIL_GPERFTOOLS_PROFILER_H_ */
