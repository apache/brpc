#include <vector>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <gflags/gflags.h>

#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/thread_local.h"
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "brpc/details/flatbuffers_impl.h"

namespace brpc {
namespace flatbuffers {

#define METHOD_SPLIT " "
#define PREFIX_SPLIT "."
#define DEFAULT_RESERVE_SIZE 64     // Reserve space for rpc header and meta

uint8_t *SlabAllocator::allocate(size_t size) {
    _old_size_param = size;
    size_t real_size = size + DEFAULT_RESERVE_SIZE;

    _full_buf_head = (uint8_t *)_iobuf.allocate(real_size);
    _fb_begin_head = _full_buf_head + DEFAULT_RESERVE_SIZE;
    return _fb_begin_head;
}

uint8_t *SlabAllocator::reallocate_downward(uint8_t *old_p,
                                        size_t old_size,
                                        size_t new_size,
                                        size_t in_use_back,
                                        size_t in_use_front) {
    _old_size_param = new_size;
    size_t new_real_size = new_size + DEFAULT_RESERVE_SIZE;

    void* old_block = _iobuf.get_cur_block();
    butil::SingleIOBuf::target_block_inc_ref(old_block);
    _full_buf_head = (uint8_t *)_iobuf.reallocate_downward(new_real_size, 0, 0);
    _fb_begin_head = _full_buf_head + DEFAULT_RESERVE_SIZE;
    memcpy_downward(old_p, old_size, _fb_begin_head, new_size, in_use_back, in_use_front);
    butil::SingleIOBuf::target_block_dec_ref(old_block);
    return _fb_begin_head;
}

void SlabAllocator::memcpy_downward(uint8_t *old_p, size_t old_size, uint8_t *new_p,
                    size_t new_size, size_t in_use_back,
                    size_t in_use_front) {
    memcpy(new_p + new_size - in_use_back, old_p + old_size - in_use_back,
        in_use_back);
    memcpy(new_p, old_p, in_use_front);
}

int ServiceDescriptor::init(const BrpcDescriptorTable& table) {
    if (table.service_name == "" || table.prefix == "" ||
                            table.method_name_list == "") {
        errno = EINVAL;
        return -1;
    }
    std::vector<std::string> res;
    std::string strs = table.method_name_list + METHOD_SPLIT;
    size_t pos = strs.find(METHOD_SPLIT);
    while(pos != strs.npos) {
        std::string tmp = strs.substr(0, pos);
        if (tmp != METHOD_SPLIT && tmp != "\n") {
            res.push_back(tmp);
        }
        strs = strs.substr(pos + 1, strs.size());
        pos = strs.find(METHOD_SPLIT);
    }
    method_count_ = res.size();
    if (method_count_ <= 0) {
        errno = EINVAL;
        return -1;
    }
    name_ = table.service_name;
    full_name_ = table.prefix + std::string(PREFIX_SPLIT) + name_;
    butil::MurmurHash3_x86_32(full_name_.c_str(), full_name_.size(), 1, &index_);
    methods_ = new MethodDescriptor*[method_count_];
    if (!methods_) {
        return -1;
    }
    memset(methods_, 0, method_count_ * sizeof(MethodDescriptor*));
    for (int i = 0; i < method_count_; ++i) {
        methods_[i] = new MethodDescriptor(table.prefix.c_str(), res[i].c_str(), this, i);
        if (!methods_[i]) {
            release_descriptor();
            return -1;
        }
    }
    return 0;
}

void ServiceDescriptor::release_descriptor() {
    if (methods_ != NULL ) {
        for (int i = 0; i < method_count_; ++i) {
            if (methods_[i] != NULL) {
                delete methods_[i];
            }
            methods_[i] = NULL;
        }
        delete methods_;
        methods_ = NULL;
    }
}

ServiceDescriptor::~ServiceDescriptor() {
    release_descriptor();
}

const MethodDescriptor* ServiceDescriptor::method(int index) const {
    if (index < 0 || index >= method_count_) {
        errno = EINVAL;
        return NULL;
    }
    if (!methods_) {
        errno = EPERM;
        return NULL;
    }
    return methods_[index];
}

MethodDescriptor::MethodDescriptor(const char* prefix,
                            const char* name,
                            const ServiceDescriptor* service,
                            int index) {
    name_ = name;
    full_name_ = std::string(prefix) + std::string(PREFIX_SPLIT) + std::string(name);
    service_ = service;
    index_ = index;
}

Message MessageBuilder::ReleaseMessage() {
    const uint8_t *msg_data = buf_.data();                  // pointer to msg
    uint32_t msg_size = static_cast<uint32_t>(buf_.size());
    butil::SingleIOBuf& iobuf = slab_allocator_.get_iobuf();
    const uint8_t *buf_data = (const uint8_t *) iobuf.get_begin();
    // Calculate offsets from the buffer start
    // reserve the memory space for rpc header and meta
    const uint8_t *data_with_rpc = msg_data - DEFAULT_RESERVE_SIZE;
    uint32_t new_msg_size = msg_size;

    new_msg_size += DEFAULT_RESERVE_SIZE;
    uint32_t begin = data_with_rpc - buf_data;

    const butil::IOBuf::BlockRef& ref = iobuf.get_cur_ref();
    butil::IOBuf::BlockRef sub_ref = {ref.offset + begin, new_msg_size, ref.block};
    Message msg(sub_ref, DEFAULT_RESERVE_SIZE, msg_size);
    Reset();
    return msg;
}

Message::Message(const butil::SingleIOBuf &iobuf,
            uint32_t meta_size = 0,
            uint32_t msg_size = 0)
                    : _iobuf(iobuf)
                    , _meta_size(meta_size)
                    , _msg_size(msg_size) {
    if (msg_size == 0) {
        _msg_size = _iobuf.get_length() - meta_size;
    }
}

Message::Message(const butil::IOBuf::BlockRef &ref,
            uint32_t meta_size = 0,
            uint32_t msg_size = 0)
                    : _iobuf(ref)
                    , _meta_size(meta_size)
                    , _msg_size(msg_size) {
    if (msg_size == 0) {
        _msg_size = _iobuf.get_length() - meta_size;
    }
}

bool Message::parse_msg_from_iobuf(const butil::IOBuf& buf, size_t msg_size, size_t meta_size) {
    size_t buf_size = buf.size();
    if (buf_size != (msg_size + meta_size)) {
        return false;
    }
    if (!_iobuf.assign(buf, buf_size)) {
        return false;
    }
    _meta_size = meta_size;
    _msg_size = msg_size;

    return true;
}

bool Message::append_msg_to_iobuf(butil::IOBuf& buf) {
    _iobuf.append_to(&buf);
    return true;
}

void *Message::reduce_meta_size_and_get_buf(uint32_t new_size) {
    if (new_size < _meta_size) {
        uint32_t off = _meta_size - new_size;
        const butil::IOBuf::BlockRef& ref = _iobuf.get_cur_ref();
        butil::IOBuf::BlockRef sub_ref = {ref.offset + off, ref.length - off, ref.block};
        _iobuf = butil::SingleIOBuf(sub_ref);
        _meta_size = new_size;
        return const_cast<void*>(_iobuf.get_begin());
    } else if (new_size == _meta_size) {
        return const_cast<void*>(_iobuf.get_begin());
    }
    LOG(WARNING) << "You should change DEFAULT_RESERVE_SIZE, new_size=" << new_size << " > _meta_size=" << _meta_size;
    return NULL;
}

int parse_service_descriptors(const BrpcDescriptorTable& descriptor_table,
                    ServiceDescriptor** descriptor_out) {
    if(descriptor_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    ServiceDescriptor *out = new ServiceDescriptor;
    if (!out) {
        return -1;
    }
    int ret = out->init(descriptor_table);
    if (ret < 0) {
        delete out;
        return -1;
    }
    *descriptor_out = out;
    return 0;
}

bool ParseFbFromIOBUF(Message* msg, size_t msg_size, const butil::IOBuf& buf, size_t meta_remaind) {
    if (!msg || msg_size == 0 || meta_remaind > msg_size) {
        return false;
    }
    return msg->parse_msg_from_iobuf(buf, msg_size, meta_remaind);
}

bool SerializeFbToIOBUF(const Message* msg, butil::IOBuf& buf) {
    if (!msg) {
        return false;
    }
    Message* msg_ptr = const_cast<Message*>(msg);
    return msg_ptr->append_msg_to_iobuf(buf);
}

}  // namespace flatbuffers
}  // namespace brpc