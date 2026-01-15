#ifndef BRPC_FLATBUFFERS_IMPL_H_
#define BRPC_FLATBUFFERS_IMPL_H_

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <flatbuffers/flatbuffers.h>
#include "butil/iobuf.h"
#include "butil/single_iobuf.h"
#include "brpc/details/flatbuffers_common.h"

namespace brpc {
namespace flatbuffers {

class MessageBuilder;

// Custom allocator for FlatBuffers that uses IOBuf as underlying storage.
// This allocator manages memory allocation for FlatBuffers messages within
// brpc's zero-copy buffer system, enabling efficient serialization and
// deserialization without unnecessary memory copies.
class SlabAllocator : public ::flatbuffers::Allocator {
public:
    SlabAllocator() {}

    SlabAllocator(const SlabAllocator &other) = delete;

    SlabAllocator &operator=(const SlabAllocator &other) = delete;

    SlabAllocator(SlabAllocator &&other) {
        // default-construct and swap idiom
        swap(other);
    }

    SlabAllocator &operator=(SlabAllocator &&other) {
    // move-construct and swap idiom
        SlabAllocator temp(std::move(other));
        swap(temp);
        return *this;
    }

    void swap(SlabAllocator &other) {
        _iobuf.swap(other._iobuf);
    }

    virtual ~SlabAllocator() {}
    /*
    * Allocate memory from the slab allocator.
    * buffer struct: fb header + fb message
    */
    virtual uint8_t *allocate(size_t size);

    virtual void deallocate(uint8_t *p, size_t size) override {
        if (p == _fb_begin_head) {
            _iobuf.deallocate((void*)_full_buf_head);
        }
    }

    void deallocate(void *p) {
        _iobuf.deallocate(p);
    }

    virtual uint8_t *reallocate_downward(uint8_t *old_p, size_t old_size,
                                        size_t new_size, size_t in_use_back,
                                        size_t in_use_front);
protected:
    void memcpy_downward(uint8_t *old_p, size_t old_size, uint8_t *new_p,
                        size_t new_size, size_t in_use_back,
                        size_t in_use_front);
private:

    butil::SingleIOBuf &get_iobuf() {
        return _iobuf;
    }
    uint8_t *_full_buf_head;
    uint8_t *_fb_begin_head;
    size_t _old_size_param;
    butil::SingleIOBuf _iobuf;
    friend class MessageBuilder;
};

// SlabAllocatorMember is a hack to ensure that the MessageBuilder's
// slab_allocator_ member is constructed before the FlatBufferBuilder, since
// the allocator is used in the FlatBufferBuilder ctor.
struct SlabAllocatorMember {
  SlabAllocator slab_allocator_;
};

// Represents a FlatBuffers message in brpc's zero-copy buffer system.
// This class wraps a FlatBuffers message stored in an IOBuf
// The message is move-only and cannot be copied.
class Message {
public:
    Message() : _meta_size(0), _msg_size(0) {}
 
    Message &operator=(const Message &other) = delete;
private:
    Message(const butil::SingleIOBuf &iobuf,
            uint32_t meta_size,
            uint32_t msg_size);

    Message(const butil::IOBuf::BlockRef &ref,
                    uint32_t meta_size,
                    uint32_t msg_size);
    friend class MessageBuilder;
public:
    Message(Message &&other) = default;

    Message(const Message &other) = delete; 

    Message &operator=(Message &&other) = default;

    void *mutable_data() const {
        return (void *)const_cast<uint8_t *>(data());
    }

    const uint8_t *data() const {
        const uint8_t *buf = (const uint8_t *)_iobuf.get_begin();
        return buf + _meta_size;
    }

    void *mutable_buf_begin() const {
        return const_cast<void*>(_iobuf.get_begin());
    }

    void *reduce_meta_size_and_get_buf(uint32_t new_size);

    uint32_t get_meta_size() const {
        return _meta_size;
    }

    size_t size() const {
        return _msg_size;
    }

    template<class T>
    bool Verify() const {
        ::flatbuffers::Verifier verifier(data(), size());
        return verifier.VerifyBuffer<T>(nullptr);
    }

    template<class T> T *GetMutableRoot() { return ::flatbuffers::GetMutableRoot<T>(mutable_data()); }
    template<class T> const T *GetRoot() const { return ::flatbuffers::GetRoot<T>(data()); }

    bool parse_msg_from_iobuf(const butil::IOBuf& buf, size_t msg_size, size_t meta_size);

    bool append_msg_to_iobuf(butil::IOBuf& buf);

    void Clear() {
        _iobuf.reset(); 
        _meta_size = 0; 
        _msg_size = 0;
    }
private:
    butil::SingleIOBuf _iobuf;
    uint32_t _meta_size;
    uint32_t _msg_size;
};

// MessageBuilder is a BRPC-specific FlatBufferBuilder that uses SlabAllocator
// to allocate BRPC buffers.
class MessageBuilder : private SlabAllocatorMember,
                       public ::flatbuffers::FlatBufferBuilder {
public:
    explicit MessageBuilder(::flatbuffers::uoffset_t initial_size = 1024)
        : ::flatbuffers::FlatBufferBuilder(initial_size, &slab_allocator_, false) {}

    MessageBuilder(const MessageBuilder &other) = delete;

    MessageBuilder &operator=(const MessageBuilder &other) = delete;

    MessageBuilder(MessageBuilder &&other)
        : ::flatbuffers::FlatBufferBuilder(1024, &slab_allocator_, false) {
        // Default construct and swap idiom.
        Swap(other);
    }

  /// Create a MessageBuilder from a FlatBufferBuilder.
    explicit MessageBuilder(::flatbuffers::FlatBufferBuilder &&src,
                        void (*dealloc)(void *) = NULL)
      : ::flatbuffers::FlatBufferBuilder(1024, &slab_allocator_, false) {
        src.Swap(*this);
        src.SwapBufAllocator(*this);
        if (buf_.capacity()) {
            uint8_t *buf = buf_.scratch_data();  // pointer to memory
            size_t capacity = buf_.capacity();   // size of memory
            slab_allocator_._iobuf.assign_user_data((void*)buf, capacity, dealloc);
        } else {
            slab_allocator_._iobuf.reset();
        }
    }

  /// Move-assign a FlatBufferBuilder to a MessageBuilder.
  /// Only FlatBufferBuilder with default allocator (basically, nullptr) is
  /// supported.
    MessageBuilder &operator=(::flatbuffers::FlatBufferBuilder &&src) {
        // Move construct a temporary and swap
        MessageBuilder temp(std::move(src));
        Swap(temp);
        return *this;
    }

    MessageBuilder &operator=(MessageBuilder &&other) {
        // Move construct a temporary and swap
        MessageBuilder temp(std::move(other));
        Swap(temp);
        return *this;
    }

    void Swap(MessageBuilder &other) {
        slab_allocator_.swap(other.slab_allocator_);
        ::flatbuffers::FlatBufferBuilder::Swap(other);
        // After swapping the FlatBufferBuilder, we swap back the allocator, which
        // restores the original allocator back in place. This is necessary because
        // MessageBuilder's allocator is its own member (SlabAllocatorMember). The
        // allocator passed to FlatBufferBuilder::vector_downward must point to this
        // member.
        buf_.swap_allocator(other.buf_);
    }

    ~MessageBuilder() {}

    // GetMessage extracts the subslab of the buffer corresponding to the
    // flatbuffers-encoded region and wraps it in a `Message` to handle buffer
    // ownership.
    // Message GetMessage() = delete;

    Message ReleaseMessage();
};

struct BrpcDescriptorTable {
    const std::string prefix;
    const std::string service_name;
    const std::string method_name_list; // split by blank
};

class ServiceDescriptor {
public:
    ServiceDescriptor() : name_("")
                        ,full_name_("")
                        ,methods_(NULL)
                        ,method_count_(0)
                        ,index_(0) {}
    ~ServiceDescriptor();
    int init(const BrpcDescriptorTable& table);
    // The name of the service, not including its containing scope.
    const std::string& name() const {return name_;}
    // The fully-qualified name of the service, scope delimited by periods.
    const std::string& full_name() const {return full_name_;}
    // Index of this service within the file's services array.
    uint32_t index() const {return index_;}
    // The number of methods this service defines.
    int method_count() const {return method_count_;}
    // Gets a MethodDescriptor by index, where 0 <= index < method_count().
    // These are returned in the order they were defined in the .proto file.
    const MethodDescriptor* method(int index) const;
private:
    void release_descriptor(); 
    std::string name_;
    std::string full_name_;
    MethodDescriptor** methods_;
    int method_count_;
    uint32_t index_;
    FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS(ServiceDescriptor);
};

int parse_service_descriptors(const BrpcDescriptorTable& descriptor_table,
                    ServiceDescriptor** descriptor_out);

class MethodDescriptor {
public:
    MethodDescriptor(const char* prefix, 
                            const char* name, 
                            const ServiceDescriptor* service, 
                            int index);
    // Name of this method, not including containing scope.
    const std::string& name() const {return name_;}
    // The fully-qualified name of the method, scope delimited by periods.
    const std::string& full_name() const {return full_name_;}
    // Index within the service's Descriptor.
    int index() const {return index_;}
    const ServiceDescriptor* service() const { return service_;}
private:
    std::string name_;
    std::string full_name_;
    const ServiceDescriptor* service_;
    int index_;
    FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS(MethodDescriptor);
};

bool ParseFbFromIOBUF(Message* msg, size_t msg_size, const butil::IOBuf& buf, size_t meta_remaind = 0);

bool SerializeFbToIOBUF(const Message* msg, butil::IOBuf& buf);

}  // namespace flatbuffers
}  // namespace brpc

#endif  // BRPC_FLATBUFFERS_IMPL_H_