// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_MEMORY_REF_COUNTED_MEMORY_H_
#define BUTIL_MEMORY_REF_COUNTED_MEMORY_H_

#include <string>
#include <vector>

#include "butil/base_export.h"
#include "butil/compiler_specific.h"
#include "butil/memory/ref_counted.h"

namespace butil {

// A generic interface to memory. This object is reference counted because one
// of its two subclasses own the data they carry, and we need to have
// heterogeneous containers of these two types of memory.
class BUTIL_EXPORT RefCountedMemory
    : public butil::RefCountedThreadSafe<RefCountedMemory> {
 public:
  // Retrieves a pointer to the beginning of the data we point to. If the data
  // is empty, this will return NULL.
  virtual const unsigned char* front() const = 0;

  // Size of the memory pointed to.
  virtual size_t size() const = 0;

  // Returns true if |other| is byte for byte equal.
  bool Equals(const scoped_refptr<RefCountedMemory>& other) const;

  // Handy method to simplify calling front() with a reinterpret_cast.
  template<typename T> const T* front_as() const {
    return reinterpret_cast<const T*>(front());
  }

 protected:
  friend class butil::RefCountedThreadSafe<RefCountedMemory>;
  RefCountedMemory();
  virtual ~RefCountedMemory();
};

// An implementation of RefCountedMemory, where the ref counting does not
// matter.
class BUTIL_EXPORT RefCountedStaticMemory : public RefCountedMemory {
 public:
  RefCountedStaticMemory()
      : data_(NULL), length_(0) {}
  RefCountedStaticMemory(const void* data, size_t length)
      : data_(static_cast<const unsigned char*>(length ? data : NULL)),
        length_(length) {}

  // Overridden from RefCountedMemory:
  virtual const unsigned char* front() const OVERRIDE;
  virtual size_t size() const OVERRIDE;

 private:
  virtual ~RefCountedStaticMemory();

  const unsigned char* data_;
  size_t length_;

  DISALLOW_COPY_AND_ASSIGN(RefCountedStaticMemory);
};

// An implementation of RefCountedMemory, where we own the data in a vector.
class BUTIL_EXPORT RefCountedBytes : public RefCountedMemory {
 public:
  RefCountedBytes();

  // Constructs a RefCountedBytes object by _copying_ from |initializer|.
  explicit RefCountedBytes(const std::vector<unsigned char>& initializer);

  // Constructs a RefCountedBytes object by copying |size| bytes from |p|.
  RefCountedBytes(const unsigned char* p, size_t size);

  // Constructs a RefCountedBytes object by performing a swap. (To non
  // destructively build a RefCountedBytes, use the constructor that takes a
  // vector.)
  static RefCountedBytes* TakeVector(std::vector<unsigned char>* to_destroy);

  // Overridden from RefCountedMemory:
  virtual const unsigned char* front() const OVERRIDE;
  virtual size_t size() const OVERRIDE;

  const std::vector<unsigned char>& data() const { return data_; }
  std::vector<unsigned char>& data() { return data_; }

 private:
  virtual ~RefCountedBytes();

  std::vector<unsigned char> data_;

  DISALLOW_COPY_AND_ASSIGN(RefCountedBytes);
};

// An implementation of RefCountedMemory, where the bytes are stored in an STL
// string. Use this if your data naturally arrives in that format.
class BUTIL_EXPORT RefCountedString : public RefCountedMemory {
 public:
  RefCountedString();

  // Constructs a RefCountedString object by performing a swap. (To non
  // destructively build a RefCountedString, use the default constructor and
  // copy into object->data()).
  static RefCountedString* TakeString(std::string* to_destroy);

  // Overridden from RefCountedMemory:
  virtual const unsigned char* front() const OVERRIDE;
  virtual size_t size() const OVERRIDE;

  const std::string& data() const { return data_; }
  std::string& data() { return data_; }

 private:
  virtual ~RefCountedString();

  std::string data_;

  DISALLOW_COPY_AND_ASSIGN(RefCountedString);
};

// An implementation of RefCountedMemory that holds a chunk of memory
// previously allocated with malloc or calloc, and that therefore must be freed
// using free().
class BUTIL_EXPORT RefCountedMallocedMemory : public butil::RefCountedMemory {
 public:
  RefCountedMallocedMemory(void* data, size_t length);

  // Overridden from RefCountedMemory:
  virtual const unsigned char* front() const OVERRIDE;
  virtual size_t size() const OVERRIDE;

 private:
  virtual ~RefCountedMallocedMemory();

  unsigned char* data_;
  size_t length_;

  DISALLOW_COPY_AND_ASSIGN(RefCountedMallocedMemory);
};

}  // namespace butil

#endif  // BUTIL_MEMORY_REF_COUNTED_MEMORY_H_
