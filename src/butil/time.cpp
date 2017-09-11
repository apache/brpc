// Copyright (c) 2010 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Fri Aug 29 15:01:15 CST 2014

#include <syscall.h>                         // SYS_clock_gettime
#include <unistd.h>                          // syscall
#include <sys/types.h>                       // open
#include <sys/stat.h>                        // ^
#include <fcntl.h>                           // ^

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>                          // memmem
#undef _GNU_SOURCE

#include "butil/time.h"

namespace butil {

clockid_t get_monotonic_clockid() {
    // http://lxr.free-electrons.com/source/include/uapi/linux/time.h#L44
    const clockid_t MY_CLOCK_MONOTONIC_RAW = 4;
    
    timespec ts;
    if (0 == syscall(SYS_clock_gettime, MY_CLOCK_MONOTONIC_RAW, &ts)) {
        return MY_CLOCK_MONOTONIC_RAW;
    }
    return CLOCK_MONOTONIC;
}

extern const clockid_t monotonic_clockid = get_monotonic_clockid();

int64_t monotonic_time_ns() {
    timespec now;
    syscall(SYS_clock_gettime, monotonic_clockid, &now);
    return now.tv_sec * 1000000000L + now.tv_nsec;
}

/*
   read_cpu_frequency() is modified from source code of glibc.
   
   Copyright (C) 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */
uint64_t read_cpu_frequency(bool* invariant_tsc) {
    /* We read the information from the /proc filesystem.  It contains at
       least one line like
       cpu MHz         : 497.840237
       or also
       cpu MHz         : 497.841
       We search for this line and convert the number in an integer.  */

    const int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    uint64_t result = 0;
    char buf[4096];  // should be enough
    const ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        char *mhz = static_cast<char*>(memmem(buf, n, "cpu MHz", 7));

        if (mhz != NULL) {
            char *endp = buf + n;
            int seen_decpoint = 0;
            int ndigits = 0;

            /* Search for the beginning of the string.  */
            while (mhz < endp && (*mhz < '0' || *mhz > '9') && *mhz != '\n') {
                ++mhz;
            }
            while (mhz < endp && *mhz != '\n') {
                if (*mhz >= '0' && *mhz <= '9') {
                    result *= 10;
                    result += *mhz - '0';
                    if (seen_decpoint)
                        ++ndigits;
                } else if (*mhz == '.') {
                    seen_decpoint = 1;
                }
                ++mhz;
            }

            /* Compensate for missing digits at the end.  */
            while (ndigits++ < 6) {
                result *= 10;
            }
        }

        if (invariant_tsc) {
            char* flags_pos = static_cast<char*>(memmem(buf, n, "flags", 5));
            *invariant_tsc = 
                (flags_pos &&
                 memmem(flags_pos, buf + n - flags_pos, "constant_tsc", 12) &&
                 memmem(flags_pos, buf + n - flags_pos, "nonstop_tsc", 11));
        }
    }
    close (fd);
    return result;
}

uint64_t read_invariant_cpu_frequency() {
    bool invariant_tsc = false;
    const uint64_t freq = read_cpu_frequency(&invariant_tsc);
    return (invariant_tsc ? freq : 0);
}

extern const uint64_t invariant_cpu_freq = read_invariant_cpu_frequency();

__thread int64_t tls_realtime_ns = 0;
__thread int64_t tls_cpuwidetime_ns = 0;

}  // namespace butil
