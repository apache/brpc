/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _THRIFT_BRPC_HELPER_TRANSPORT_H_
#define _THRIFT_BRPC_HELPER_TRANSPORT_H_ 1

#include <cstdlib>
#include <cstring>
#include <limits>
#include <boost/scoped_array.hpp>

#include <thrift/transport/TTransport.h>
#include <thrift/transport/TVirtualTransport.h>
#include <thrift/transport/TBufferTransports.h>

#include "butil/iobuf.h"
#include <brpc/channel.h>

#ifdef __GNUC__
#define TDB_LIKELY(val) (__builtin_expect((val), 1))
#define TDB_UNLIKELY(val) (__builtin_expect((val), 0))
#else
#define TDB_LIKELY(val) (val)
#define TDB_UNLIKELY(val) (val)
#endif

namespace apache {
namespace thrift {
namespace transport {

/**
 * A memory buffer is a tranpsort that simply reads from and writes to an
 * in memory buffer. Anytime you call write on it, the data is simply placed
 * into a buffer, and anytime you call read, data is read from that buffer.
 *
 * The buffers are allocated using C constructs malloc,realloc, and the size
 * doubles as necessary.  We've considered using scoped
 *
 */
class TThriftBrpcHelperTransport : public TVirtualTransport<TThriftBrpcHelperTransport, TBufferBase> {
private:
  // Common initialization done by all constructors.
  void initCommon(uint8_t* buf, uint32_t size, bool owner, uint32_t wPos) {
    if (buf == NULL && size != 0) {
      assert(owner);
      buf = (uint8_t*)std::malloc(size);
      if (buf == NULL) {
        throw std::bad_alloc();
      }
    }

    buffer_ = buf;
    bufferSize_ = size;

    rBase_ = buffer_;
    rBound_ = buffer_ + wPos;
    // TODO(dreiss): Investigate NULL-ing this if !owner.
    wBase_ = buffer_ + wPos;
    wBound_ = buffer_ + bufferSize_;

    owner_ = owner;

    // rBound_ is really an artifact.  In principle, it should always be
    // equal to wBase_.  We update it in a few places (computeRead, etc.).
  }

public:
  static const uint32_t defaultSize = 1024;

  /**
   * This enum specifies how a TThriftBrpcHelperTransport should treat
   * memory passed to it via constructors or resetBuffer.
   *
   * OBSERVE:
   *   TThriftBrpcHelperTransport will simply store a pointer to the memory.
   *   It is the callers responsibility to ensure that the pointer
   *   remains valid for the lifetime of the TThriftBrpcHelperTransport,
   *   and that it is properly cleaned up.
   *   Note that no data can be written to observed buffers.
   *
   * COPY:
   *   TThriftBrpcHelperTransport will make an internal copy of the buffer.
   *   The caller has no responsibilities.
   *
   * TAKE_OWNERSHIP:
   *   TThriftBrpcHelperTransport will become the "owner" of the buffer,
   *   and will be responsible for freeing it.
   *   The membory must have been allocated with malloc.
   */
  enum MemoryPolicy { OBSERVE = 1, COPY = 2, TAKE_OWNERSHIP = 3 };

  /**
   * Construct a TThriftBrpcHelperTransport with a default-sized buffer,
   * owned by the TThriftBrpcHelperTransport object.
   */
  TThriftBrpcHelperTransport() { initCommon(NULL, defaultSize, true, 0); }

  /**
   * Construct a TThriftBrpcHelperTransport with a buffer of a specified size,
   * owned by the TThriftBrpcHelperTransport object.
   *
   * @param sz  The initial size of the buffer.
   */
  TThriftBrpcHelperTransport(uint32_t sz) { initCommon(NULL, sz, true, 0); }

  /**
   * Construct a TThriftBrpcHelperTransport with buf as its initial contents.
   *
   * @param buf    The initial contents of the buffer.
   *               Note that, while buf is a non-const pointer,
   *               TThriftBrpcHelperTransport will not write to it if policy == OBSERVE,
   *               so it is safe to const_cast<uint8_t*>(whatever).
   * @param sz     The size of @c buf.
   * @param policy See @link MemoryPolicy @endlink .
   */
  TThriftBrpcHelperTransport(uint8_t* buf, uint32_t sz, MemoryPolicy policy = OBSERVE) {
    if (buf == NULL && sz != 0) {
      throw TTransportException(TTransportException::BAD_ARGS,
                                "TThriftBrpcHelperTransport given null buffer with non-zero size.");
    }

    switch (policy) {
    case OBSERVE:
    case TAKE_OWNERSHIP:
      initCommon(buf, sz, policy == TAKE_OWNERSHIP, sz);
      break;
    case COPY:
      initCommon(NULL, sz, true, 0);
      this->write(buf, sz);
      break;
    default:
      throw TTransportException(TTransportException::BAD_ARGS,
                                "Invalid MemoryPolicy for TThriftBrpcHelperTransport");
    }
  }

  ~TThriftBrpcHelperTransport() {
    if (owner_) {
      std::free(buffer_);
    }
  }

  void set_controller(brpc::Controller* cntl) {
    cntl_ = cntl;
  }

  void set_channel(brpc::Channel* channel) {
    channel_ = channel;
  }

  bool isOpen() { return true; }

  bool peek() { return (rBase_ < wBase_); }

  void open() {}

  void close() {}

  // TODO(dreiss): Make bufPtr const.
  void getBuffer(uint8_t** bufPtr, uint32_t* sz) {
    *bufPtr = rBase_;
    *sz = static_cast<uint32_t>(wBase_ - rBase_);
  }

  std::string getBufferAsString() {
    if (buffer_ == NULL) {
      return "";
    }
    uint8_t* buf;
    uint32_t sz;
    getBuffer(&buf, &sz);
    return std::string((char*)buf, (std::string::size_type)sz);
  }

  void appendBufferToString(std::string& str) {
    if (buffer_ == NULL) {
      return;
    }
    uint8_t* buf;
    uint32_t sz;
    getBuffer(&buf, &sz);
    str.append((char*)buf, sz);
  }

  void resetBuffer() {
    rBase_ = buffer_;
    rBound_ = buffer_;
    wBase_ = buffer_;
    // It isn't safe to write into a buffer we don't own.
    if (!owner_) {
      wBound_ = wBase_;
      bufferSize_ = 0;
    }
  }

  /// See constructor documentation.
  void resetBuffer(uint8_t* buf, uint32_t sz, MemoryPolicy policy = OBSERVE) {
    // Use a variant of the copy-and-swap trick for assignment operators.
    // This is sub-optimal in terms of performance for two reasons:
    //   1/ The constructing and swapping of the (small) values
    //      in the temporary object takes some time, and is not necessary.
    //   2/ If policy == COPY, we allocate the new buffer before
    //      freeing the old one, precluding the possibility of
    //      reusing that memory.
    // I doubt that either of these problems could be optimized away,
    // but the second is probably no a common case, and the first is minor.
    // I don't expect resetBuffer to be a common operation, so I'm willing to
    // bite the performance bullet to make the method this simple.

    // Construct the new buffer.
    TThriftBrpcHelperTransport new_buffer(buf, sz, policy);
    // Move it into ourself.
    this->swap(new_buffer);
    // Our old self gets destroyed.
  }

  /// See constructor documentation.
  void resetBuffer(uint32_t sz) {
    // Construct the new buffer.
    TThriftBrpcHelperTransport new_buffer(sz);
    // Move it into ourself.
    this->swap(new_buffer);
    // Our old self gets destroyed.
  }

  std::string readAsString(uint32_t len) {
    std::string str;
    (void)readAppendToString(str, len);
    return str;
  }

  uint32_t readAppendToString(std::string& str, uint32_t len);

  // return number of bytes read
  uint32_t readEnd();

  // Return number of bytes written
  uint32_t writeEnd();

  uint32_t available_read() const {
    // Remember, wBase_ is the real rBound_.
    return static_cast<uint32_t>(wBase_ - rBase_);
  }

  uint32_t available_write() const { return static_cast<uint32_t>(wBound_ - wBase_); }

  // Returns a pointer to where the client can write data to append to
  // the TThriftBrpcHelperTransport, and ensures the buffer is big enough to accommodate a
  // write of the provided length.  The returned pointer is very convenient for
  // passing to read(), recv(), or similar. You must call wroteBytes() as soon
  // as data is written or the buffer will not be aware that data has changed.
  uint8_t* getWritePtr(uint32_t len) {
    ensureCanWrite(len);
    return wBase_;
  }

  // Informs the buffer that the client has written 'len' bytes into storage
  // that had been provided by getWritePtr().
  void wroteBytes(uint32_t len);

  /*
   * TVirtualTransport provides a default implementation of readAll().
   * We want to use the TBufferBase version instead.
   */
  uint32_t readAll(uint8_t* buf, uint32_t len) { return TBufferBase::readAll(buf, len); }

protected:
  void swap(TThriftBrpcHelperTransport& that) {
    using std::swap;
    swap(buffer_, that.buffer_);
    swap(bufferSize_, that.bufferSize_);

    swap(rBase_, that.rBase_);
    swap(rBound_, that.rBound_);
    swap(wBase_, that.wBase_);
    swap(wBound_, that.wBound_);

    swap(owner_, that.owner_);
  }

  // Make sure there's at least 'len' bytes available for writing.
  void ensureCanWrite(uint32_t len);

  // Compute the position and available data for reading.
  void computeRead(uint32_t len, uint8_t** out_start, uint32_t* out_give);

  uint32_t readSlow(uint8_t* buf, uint32_t len);

  void writeSlow(const uint8_t* buf, uint32_t len);

  const uint8_t* borrowSlow(uint8_t* buf, uint32_t* len);

  // Data buffer
  uint8_t* buffer_;

  // Allocated buffer size
  uint32_t bufferSize_;

  // Is this object the owner of the buffer?
  bool owner_;

  brpc::Controller* cntl_;

  brpc::Channel* channel_;

  // Don't forget to update constrctors, initCommon, and swap if
  // you add new members.
};
}
}
} // apache::thrift::transport

#endif // #ifndef _THRIFT_BRPC_HELPER_TRANSPORT_H_
  
