// Copyright (c) 2018 brpc authors.
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

// Author: Ge,Jun (jge666@gmail.com)
// Date: Wed Aug  8 05:51:33 PDT 2018

#ifndef BUTIL_READER_WRITER_H
#define BUTIL_READER_WRITER_H

#include <sys/uio.h>                             // iovec

namespace butil {

// Abstraction for reading data.
// The simplest implementation is to embed a file descriptor and read from it.
class IReader {
public:
    virtual ~IReader() {}

    // Semantics of parameters and return value are same as readv(2) except that
    // there's no `fd'.
    virtual ssize_t ReadV(const iovec* iov, int iovcnt) = 0;
};

// Abstraction for writing data.
// The simplest implementation is to embed a file descriptor and writev into it.
class IWriter {
public:
    virtual ~IWriter() {}

    // Semantics of parameters and return value are same as writev(2) except that
    // there's no `fd'.
    // WriteV is required to submit data gathered by multiple appends in one 
    // run and enable the possibility of atomic writes.
    virtual ssize_t WriteV(const iovec* iov, int iovcnt) = 0;
};

}  // namespace butil

#endif  // BUTIL_READER_WRITER_H
