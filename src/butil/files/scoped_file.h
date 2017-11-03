// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_FILES_SCOPED_FILE_H_
#define BUTIL_FILES_SCOPED_FILE_H_

#include <stdio.h>

#include "butil/base_export.h"
#include "butil/logging.h"
#include "butil/memory/scoped_ptr.h"
#include "butil/scoped_generic.h"
#include "butil/build_config.h"

namespace butil {

namespace internal {

#if defined(OS_POSIX)
struct BUTIL_EXPORT ScopedFDCloseTraits {
  static int InvalidValue() {
    return -1;
  }
  static void Free(int fd);
};
#endif

}  // namespace internal

// -----------------------------------------------------------------------------

#if defined(OS_POSIX)
// A low-level Posix file descriptor closer class. Use this when writing
// platform-specific code, especially that does non-file-like things with the
// FD (like sockets).
//
// If you're writing low-level Windows code, see butil/win/scoped_handle.h
// which provides some additional functionality.
//
// If you're writing cross-platform code that deals with actual files, you
// should generally use butil::File instead which can be constructed with a
// handle, and in addition to handling ownership, has convenient cross-platform
// file manipulation functions on it.
typedef ScopedGeneric<int, internal::ScopedFDCloseTraits> ScopedFD;
#endif

//// Automatically closes |FILE*|s.
class ScopedFILE {
    MOVE_ONLY_TYPE_FOR_CPP_03(ScopedFILE, RValue);
public:
    ScopedFILE() : _fp(NULL) {}

    // Open file at |path| with |mode|.
    // If fopen failed, operator FILE* returns NULL and errno is set.
    ScopedFILE(const char *path, const char *mode) {
        _fp = fopen(path, mode);
    }

    // |fp| must be the return value of fopen as we use fclose in the
    // destructor, otherwise the behavior is undefined
    explicit ScopedFILE(FILE *fp) :_fp(fp) {}

    ScopedFILE(RValue rvalue) {
        _fp = rvalue.object->_fp;
        rvalue.object->_fp = NULL;
    }

    ~ScopedFILE() {
        if (_fp != NULL) {
            fclose(_fp);
            _fp = NULL;
        }
    }

    // Close current opened file and open another file at |path| with |mode|
    void reset(const char* path, const char* mode) {
        reset(fopen(path, mode));
    }

    void reset() { reset(NULL); }

    void reset(FILE *fp) {
        if (_fp != NULL) {
            fclose(_fp);
            _fp = NULL;
        }
        _fp = fp;
    }

    // Set internal FILE* to NULL and return previous value.
    FILE* release() {
        FILE* const prev_fp = _fp;
        _fp = NULL;
        return prev_fp;
    }
    
    operator FILE*() const { return _fp; }

    FILE* get() { return _fp; }
    
private:
    FILE* _fp;
};

}  // namespace butil

#endif  // BUTIL_FILES_SCOPED_FILE_H_
