// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#ifndef BRPC_PROFILER_LINKER_H
#define BRPC_PROFILER_LINKER_H

#if defined(BRPC_ENABLE_CPU_PROFILER) || defined(BAIDU_RPC_ENABLE_CPU_PROFILER)
#ifdef __cplusplus
extern "C" {
#endif
// Suppress warning of `google/profiler.h' which has been deprecated since
// gperftools-2.2
void ProfilerStart(const char*);
void ProfilerStop(const char*);
#ifdef __cplusplus
}
#endif
#endif

#if defined(BRPC_ENABLE_HEAP_PROFILER) || defined(BAIDU_RPC_ENABLE_HEAP_PROFILER)
#include <string>
void TCMallocGetHeapSample(std::string* writer);
void TCMallocGetHeapGrowthStacks(std::string* writer);
#endif


namespace brpc {

// defined in src/brpc/builtin/index_service.cpp
extern bool cpu_profiler_enabled;
extern bool heap_profiler_enabled;

// defined in src/brpc/controller.cpp
extern int PROFILER_LINKER_DUMMY;

struct ProfilerLinker {
    // [ Must be inlined ]
    // This function is included by user's compilation unit to force
    // linking of ProfilerStart()/ProfilerStop()/TCMallocGetHeapSample()
    // etc when corresponding macros are defined.
    inline ProfilerLinker() {
        
#if defined(BRPC_ENABLE_CPU_PROFILER) || defined(BAIDU_RPC_ENABLE_CPU_PROFILER)
        cpu_profiler_enabled = true;
        // compiler has no way to tell if PROFILER_LINKER_DUMMY is 0 or not,
        // so it has to link the function inside the branch.
        if (PROFILER_LINKER_DUMMY != 0/*must be false*/) {
            ProfilerStart("this_function_should_never_run");
        }
#endif
    
#if defined(BRPC_ENABLE_HEAP_PROFILER) || defined(BAIDU_RPC_ENABLE_HEAP_PROFILER)
        heap_profiler_enabled = true;
        if (PROFILER_LINKER_DUMMY != 0/*must be false*/) {
            TCMallocGetHeapSample(NULL);
            TCMallocGetHeapGrowthStacks(NULL);
        }
#endif
    }
};

} // namespace brpc


#endif  // BRPC_PROFILER_LINKER_H
