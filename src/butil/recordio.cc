#include <gflags/gflags.h>
#include "butil/logging.h"
#include "butil/recordio.h"
#include "butil/sys_byteorder.h"

namespace butil {

DEFINE_int64(recordio_max_record_size, 67108864,
             "Records exceeding this size will be rejected");

#define BRPC_RECORDIO_MAGIC "RDIO"

const size_t MAX_NAME_SIZE = 256;

// 8-bit CRC using the polynomial x^8+x^6+x^3+x^2+1, 0x14D.
// Chosen based on Koopman, et al. (0xA6 in his notation = 0x14D >> 1):
// http://www.ece.cmu.edu/~koopman/roses/dsn04/koopman04_crc_poly_embedded.pdf
//
// This implementation is reflected, processing the least-significant bit of the
// input first, has an initial CRC register value of 0xff, and exclusive-or's
// the final register value with 0xff. As a result the CRC of an empty string,
// and therefore the initial CRC value, is zero.
//
// The standard description of this CRC is:
// width=8 poly=0x4d init=0xff refin=true refout=true xorout=0xff check=0xd8
// name="CRC-8/KOOP"
static unsigned char const crc8_table[] = {
    0xea, 0xd4, 0x96, 0xa8, 0x12, 0x2c, 0x6e, 0x50, 0x7f, 0x41, 0x03, 0x3d,
    0x87, 0xb9, 0xfb, 0xc5, 0xa5, 0x9b, 0xd9, 0xe7, 0x5d, 0x63, 0x21, 0x1f,
    0x30, 0x0e, 0x4c, 0x72, 0xc8, 0xf6, 0xb4, 0x8a, 0x74, 0x4a, 0x08, 0x36,
    0x8c, 0xb2, 0xf0, 0xce, 0xe1, 0xdf, 0x9d, 0xa3, 0x19, 0x27, 0x65, 0x5b,
    0x3b, 0x05, 0x47, 0x79, 0xc3, 0xfd, 0xbf, 0x81, 0xae, 0x90, 0xd2, 0xec,
    0x56, 0x68, 0x2a, 0x14, 0xb3, 0x8d, 0xcf, 0xf1, 0x4b, 0x75, 0x37, 0x09,
    0x26, 0x18, 0x5a, 0x64, 0xde, 0xe0, 0xa2, 0x9c, 0xfc, 0xc2, 0x80, 0xbe,
    0x04, 0x3a, 0x78, 0x46, 0x69, 0x57, 0x15, 0x2b, 0x91, 0xaf, 0xed, 0xd3,
    0x2d, 0x13, 0x51, 0x6f, 0xd5, 0xeb, 0xa9, 0x97, 0xb8, 0x86, 0xc4, 0xfa,
    0x40, 0x7e, 0x3c, 0x02, 0x62, 0x5c, 0x1e, 0x20, 0x9a, 0xa4, 0xe6, 0xd8,
    0xf7, 0xc9, 0x8b, 0xb5, 0x0f, 0x31, 0x73, 0x4d, 0x58, 0x66, 0x24, 0x1a,
    0xa0, 0x9e, 0xdc, 0xe2, 0xcd, 0xf3, 0xb1, 0x8f, 0x35, 0x0b, 0x49, 0x77,
    0x17, 0x29, 0x6b, 0x55, 0xef, 0xd1, 0x93, 0xad, 0x82, 0xbc, 0xfe, 0xc0,
    0x7a, 0x44, 0x06, 0x38, 0xc6, 0xf8, 0xba, 0x84, 0x3e, 0x00, 0x42, 0x7c,
    0x53, 0x6d, 0x2f, 0x11, 0xab, 0x95, 0xd7, 0xe9, 0x89, 0xb7, 0xf5, 0xcb,
    0x71, 0x4f, 0x0d, 0x33, 0x1c, 0x22, 0x60, 0x5e, 0xe4, 0xda, 0x98, 0xa6,
    0x01, 0x3f, 0x7d, 0x43, 0xf9, 0xc7, 0x85, 0xbb, 0x94, 0xaa, 0xe8, 0xd6,
    0x6c, 0x52, 0x10, 0x2e, 0x4e, 0x70, 0x32, 0x0c, 0xb6, 0x88, 0xca, 0xf4,
    0xdb, 0xe5, 0xa7, 0x99, 0x23, 0x1d, 0x5f, 0x61, 0x9f, 0xa1, 0xe3, 0xdd,
    0x67, 0x59, 0x1b, 0x25, 0x0a, 0x34, 0x76, 0x48, 0xf2, 0xcc, 0x8e, 0xb0,
    0xd0, 0xee, 0xac, 0x92, 0x28, 0x16, 0x54, 0x6a, 0x45, 0x7b, 0x39, 0x07,
    0xbd, 0x83, 0xc1, 0xff
};

static uint8_t SizeChecksum(uint32_t input) {
    uint8_t crc = 0;
    crc = crc8_table[crc ^ (input & 0xFF)];
    crc = crc8_table[crc ^ ((input >> 8) & 0xFF)];
    crc = crc8_table[crc ^ ((input >> 16) & 0xFF)];
    crc = crc8_table[crc ^ ((input >> 24) & 0xFF)];
    return crc;
}

const butil::IOBuf* Record::Meta(const char* name) const {
    for (size_t i = 0; i < _metas.size(); ++i) {
        if (_metas[i].name == name) {
            return _metas[i].data.get();
        }
    }
    return NULL;
}

butil::IOBuf* Record::MutableMeta(const char* name_cstr, bool null_on_found) {
    const butil::StringPiece name = name_cstr;
    for (size_t i = 0; i < _metas.size(); ++i) {
        if (_metas[i].name == name) {
            return null_on_found ? NULL : _metas[i].data.get();
        }
    }
    if (name.size() > MAX_NAME_SIZE) {
        LOG(ERROR) << "Too long name=" << name;
        return NULL;
    } else if (name.empty()) {
        LOG(ERROR) << "Empty name";
        return NULL;
    }
    NamedMeta p;
    name.CopyToString(&p.name);
    p.data = std::make_shared<butil::IOBuf>();
    _metas.push_back(p);
    return p.data.get();
}

butil::IOBuf* Record::MutableMeta(const std::string& name, bool null_on_found) {
    for (size_t i = 0; i < _metas.size(); ++i) {
        if (_metas[i].name == name) {
            return null_on_found ? NULL : _metas[i].data.get();
        }
    }
    if (name.size() > MAX_NAME_SIZE) {
        LOG(ERROR) << "Too long name" << name;
        return NULL;
    } else if (name.empty()) {
        LOG(ERROR) << "Empty name";
        return NULL;
    }
    NamedMeta p;
    p.name = name;
    p.data = std::make_shared<butil::IOBuf>();
    _metas.push_back(p);
    return p.data.get();
}

bool Record::RemoveMeta(const butil::StringPiece& name) {
    for (size_t i = 0; i < _metas.size(); ++i) {
        if (_metas[i].name == name) {
            _metas[i] = _metas.back();
            _metas.pop_back();
            return true;
        }
    }
    return false;
}

void Record::Clear() {
    _payload.clear();
    _metas.clear();
}

size_t Record::ByteSize() const {
    size_t n = 9 + _payload.size();
    for (size_t i = 0; i < _metas.size(); ++i) {
        const NamedMeta& m = _metas[i];
        n += 5 + m.name.size() + m.data->size();
    }
    return n;
}

RecordReader::RecordReader(IReader* reader)
    : _reader(reader)
    , _cutter(&_portal)
    , _ncut(0)
    , _last_error(0) {
}

bool RecordReader::ReadNext(Record* out) {
    const size_t MAX_READ = 1024 * 1024;
    do {
        const int rc = CutRecord(out);
        if (rc > 0) {
            _last_error = 0;
            return true;
        } else if (rc < 0) {
            while (!CutUntilNextRecordCandidate()) {
                const ssize_t nr = _portal.append_from_reader(_reader, MAX_READ);
                if (nr <= 0) {
                    _last_error = (nr < 0 ? errno : END_OF_READER);
                    return false;
                }
            }
        } else { // rc == 0, not enough data to parse
            const ssize_t nr = _portal.append_from_reader(_reader, MAX_READ);
            if (nr <= 0) {
                _last_error = (nr < 0 ? errno : END_OF_READER);
                return false;
            }
        }
    } while (true);
}

bool RecordReader::CutUntilNextRecordCandidate() {
    const size_t old_ncut = _ncut;
    // Skip beginning magic
    char magic[4];
    if (_cutter.copy_to(magic, sizeof(magic)) != sizeof(magic)) {
        return false;
    }
    void* dummy = magic;  // suppressing strict-aliasing warning
    if (*(const uint32_t*)dummy == *(const uint32_t*)BRPC_RECORDIO_MAGIC) {
        _cutter.pop_front(sizeof(magic));
        _ncut += sizeof(magic);
    }
    char buf[512];
    do {
        const size_t nc = _cutter.copy_to(buf, sizeof(buf));
        if (nc < sizeof(magic)) {
            return false;
        }
        const size_t m = nc + 1 - sizeof(magic);
        for (size_t i = 0; i < m; ++i) {
            void* dummy = buf + i;  // suppressing strict-aliasing warning
            if (*(const uint32_t*)dummy == *(const uint32_t*)BRPC_RECORDIO_MAGIC) {
                _cutter.pop_front(i);
                _ncut += i;
                LOG(INFO) << "Found record candidate after " << _ncut - old_ncut << " bytes";
                return true;
            }
        }
        _cutter.pop_front(m);
        _ncut += m;
        if (nc < sizeof(buf)) {
            return false;
        }
    } while (true);
}

int RecordReader::CutRecord(Record* rec) {
    uint8_t headbuf[9];
    if (_cutter.copy_to(headbuf, sizeof(headbuf)) != sizeof(headbuf)) {
        return 0;
    }
    void* dummy = headbuf;  // suppressing strict-aliasing warning
    if (*(const uint32_t*)dummy != *(const uint32_t*)BRPC_RECORDIO_MAGIC) {
        LOG(ERROR) << "Invalid magic_num="
                   << butil::PrintedAsBinary(std::string((char*)headbuf, 4))
                   << ", offset=" << offset();
        return -1;
    }
    uint32_t tmp = NetToHost32(*(const uint32_t*)(headbuf + 4));
    const uint8_t checksum = SizeChecksum(tmp);
    bool has_meta = (tmp & 0x80000000);
    // NOTE: use size_t rather than uint32_t for sizes to avoid potential
    // addition overflows
    const size_t data_size = (tmp & 0x7FFFFFFF);
    if (checksum != headbuf[8]) {
        LOG(ERROR) << "Unmatched checksum of 0x"
                   << std::hex << tmp << std::dec
                   << "(metabit=" << has_meta
                   << " size=" << data_size
                   << " offset=" << offset()
                   << "), expected=" << (unsigned)headbuf[8]
                   << " actual=" << (unsigned)checksum;
        return -1;
    }
    if (data_size > (size_t)FLAGS_recordio_max_record_size) {
        LOG(ERROR) << "data_size=" << data_size
                   << " is larger than -recordio_max_record_size="
                   << FLAGS_recordio_max_record_size
                   << ", offset=" << offset();
        return -1;
    }
    if (_cutter.remaining_bytes() < data_size) {
        return 0;
    }
    rec->Clear();
    _cutter.pop_front(sizeof(headbuf));
    _ncut += sizeof(headbuf);
    size_t consumed_bytes = 0;
    while (has_meta) {
        char name_size_buf = 0;
        CHECK(_cutter.cut1(&name_size_buf));
        const size_t name_size = (uint8_t)name_size_buf;
        std::string name;
        _cutter.cutn(&name, name_size);
        _cutter.cutn(&tmp, 4);
        tmp = NetToHost32(tmp);
        has_meta = (tmp & 0x80000000);
        const size_t meta_size = (tmp & 0x7FFFFFFF);
        _ncut += 5 + name_size;
        if (consumed_bytes + 5 + name_size + meta_size > data_size) {
            LOG(ERROR) << name << ".meta_size=" << meta_size
                       << " is inconsistent with its data_size=" << data_size
                       << ", offset=" << offset();
            return -1;
        }
        butil::IOBuf* meta = rec->MutableMeta(name, true/*null_on_found*/);
        if (meta == NULL) {
            LOG(ERROR) << "Fail to add meta=" << name
                       << ", offset=" << offset();
            return -1;
        }
        _cutter.cutn(meta, meta_size);
        _ncut += meta_size;
        consumed_bytes += 5 + name_size + meta_size;
    }
    _cutter.cutn(rec->MutablePayload(), data_size - consumed_bytes);
    _ncut += data_size - consumed_bytes;
    return 1;
}

RecordWriter::RecordWriter(IWriter* writer)
    :_writer(writer) {
}

int RecordWriter::WriteWithoutFlush(const Record& rec) {
    const size_t old_size = _buf.size();
    uint8_t headbuf[9];
    const IOBuf::Area headarea = _buf.reserve(sizeof(headbuf));
    for (size_t i = 0; i < rec.MetaCount(); ++i) {
        auto& s = rec.MetaAt(i);
        if (s.name.size() > MAX_NAME_SIZE) {
            LOG(ERROR) << "Too long name=" << s.name;
            _buf.pop_back(_buf.size() - old_size);
            return -1;
        }
        char metabuf[s.name.size() + 5];
        char* p = metabuf;
        *p = s.name.size();
        ++p;
        memcpy(p, s.name.data(), s.name.size());
        p += s.name.size();
        if (s.data->size() > 0x7FFFFFFFULL) {
            LOG(ERROR) << "Meta named `" << s.name << "' is too long, size="
                       << s.data->size();
            _buf.pop_back(_buf.size() - old_size);
            return -1;
        }
        uint32_t tmp = s.data->size() & 0x7FFFFFFF;
        if (i < rec.MetaCount() - 1) {
            tmp |= 0x80000000;
        }
        *(uint32_t*)p = HostToNet32(tmp);
        _buf.append(metabuf, sizeof(metabuf));
        _buf.append(*s.data.get());
    }
    if (!rec.Payload().empty()) {
        _buf.append(rec.Payload());
    }
    void* dummy = headbuf;  // suppressing strict-aliasing warning
    *(uint32_t*)dummy = *(const uint32_t*)BRPC_RECORDIO_MAGIC;
    const size_t data_size = _buf.size() - old_size - sizeof(headbuf);
    if (data_size > 0x7FFFFFFFULL) {
        LOG(ERROR) << "data_size=" << data_size << " is too long";
        _buf.pop_back(_buf.size() - old_size);
        return -1;
    }
    uint32_t tmp = (data_size & 0x7FFFFFFF);
    if (rec.MetaCount() > 0) {
        tmp |= 0x80000000;
    }
    *(uint32_t*)(headbuf + 4) = HostToNet32(tmp);
    headbuf[8] = SizeChecksum(tmp);
    _buf.unsafe_assign(headarea, headbuf);
    return 0;
}

int RecordWriter::Flush() {
    size_t total_nw = 0;
    do {
        const ssize_t nw = _buf.cut_into_writer(_writer);
        if (nw > 0) {
            total_nw += nw;
        } else {
            if (total_nw) {
                // We've flushed sth., return as success.
                return 0;
            }
            if (nw == 0) {
                return _buf.empty() ? 0 : EAGAIN;
            } else {
                return errno;
            }
        }
    } while (true);
}

int RecordWriter::Write(const Record& record) {
    const int rc = WriteWithoutFlush(record);
    if (rc) {
        return rc;
    }
    return Flush();
}


} // namespace butil
