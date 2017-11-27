// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu, Inc.
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
// Date: Wed Jul 30 11:47:19 CST 2014

#ifndef BTHREAD_ERRNO_H
#define BTHREAD_ERRNO_H

#include <errno.h>                    // errno
#include "butil/errno.h"               // berror(), DEFINE_BTHREAD_ERRNO

__BEGIN_DECLS

extern int *bthread_errno_location();

#ifdef errno
#undef errno
#define errno *bthread_errno_location()
#endif

// List errno used throughout bthread
extern const int ESTOP;

__END_DECLS

#endif  //BTHREAD_ERRNO_H
