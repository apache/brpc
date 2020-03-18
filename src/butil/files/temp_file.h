// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Date: Thu Oct 28 15:23:09 2010

#ifndef BUTIL_FILES_TEMP_FILE_H
#define BUTIL_FILES_TEMP_FILE_H

#include "butil/macros.h"

namespace butil {

// Create a temporary file in current directory, which will be deleted when 
// corresponding TempFile object destructs, typically for unit testing.
// 
// Usage:
//   { 
//      TempFile tmpfile;           // A temporay file shall be created
//      tmpfile.save("some text");  // Write into the temporary file
//   }
//   // The temporary file shall be removed due to destruction of tmpfile
 
class TempFile {
public:
    // Create a temporary file in current directory. If |ext| is given,
    // filename will be temp_file_XXXXXX.|ext|, temp_file_XXXXXX otherwise.
    // If temporary file cannot be created, all save*() functions will
    // return -1. If |ext| is too long, filename will be truncated.
    TempFile();
    explicit TempFile(const char* ext);

    // The temporary file is removed in destructor.
    ~TempFile();

    // Save |content| to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save(const char *content);

    // Save |fmt| and associated values to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save_format(const char *fmt, ...) __attribute__((format (printf, 2, 3) ));

    // Save binary data |buf| (|count| bytes) to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save_bin(const void *buf, size_t count);
    
    // Get name of the temporary file.
    const char *fname() const { return _fname; }

private:
    // TempFile is associated with file, copying makes no sense.
    DISALLOW_COPY_AND_ASSIGN(TempFile);
    
    int _reopen_if_necessary();
    
    int _fd;                // file descriptor
    int _ever_opened;
    char _fname[24];        // name of the file
};

} // namespace butil

#endif  // BUTIL_FILES_TEMP_FILE_H
