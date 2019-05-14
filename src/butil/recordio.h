// recordio - A binary format to transport data from end to end.
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
// Date: Thu Nov 22 13:57:56 CST 2012

#ifndef BUTIL_RECORDIO_H
#define BUTIL_RECORDIO_H

#include "butil/iobuf.h"
#include <memory>

namespace butil {

class Record {
public:
    struct NamedMeta {
        std::string name;
        std::shared_ptr<butil::IOBuf> data;
    };

    // Number of meta
    size_t MetaCount() const { return _metas.size(); }

    // Get i-th Meta, out-of-range accesses may crash.
    const NamedMeta& MetaAt(size_t i) const { return _metas[i]; }

    // Get meta by name.
    const butil::IOBuf* Meta(const char* name) const;

    // Add meta.
    // Returns a modifiable pointer to the meta with the name.
    // If null_on_found is true and meta with the name is present, NULL is returned.
    butil::IOBuf* MutableMeta(const char* name, bool null_on_found = false);
    butil::IOBuf* MutableMeta(const std::string& name, bool null_on_found = false);

    // Remove meta with the name.
    // Returns true on erased.
    bool RemoveMeta(const butil::StringPiece& name);

    // Get the payload.
    const butil::IOBuf& Payload() const { return _payload; }

    // Get a modifiable pointer to the payload.
    butil::IOBuf* MutablePayload() { return &_payload; }

    // Clear payload and remove all meta.
    void Clear();

    // Byte size of serialized form of this record.
    size_t ByteSize() const;

private:
    butil::IOBuf _payload;
    std::vector<NamedMeta> _metas;
};

// Parse records from the IReader, corrupted records will be skipped.
// Example:
//    RecordReader rd(ireader);
//    Record rec;
//    while (rd.ReadNext(&rec)) {
//        HandleRecord(rec);
//    }
//    if (rd.last_error() != RecordReader::END_OF_READER) {
//        LOG(FATAL) << "Critical error occurred";
//    }
class RecordReader {
public:
    static const int END_OF_READER = -1;

    explicit RecordReader(IReader* reader);
    
    // Returns true on success and `out' is overwritten by the record.
    // False otherwise, check last_error() for the error which is treated as permanent.
    bool ReadNext(Record* out);

    // 0 means no error.
    // END_OF_READER means all bytes in the input reader are consumed and
    // turned into records.
    int last_error() const { return _last_error; }

    // Total bytes of all read records.
    size_t read_bytes() const { return _ncut; }

private:
    bool CutUntilNextRecordCandidate();
    int CutRecord(Record* rec);

private:
    IReader* _reader;
    IOPortal _portal;
    IOBufCutter _cutter;
    size_t _ncut;
    int _last_error;
};

// Write records into the IWriter.
class RecordWriter {
public:
    explicit RecordWriter(IWriter* writer);
    
    // Serialize the record into internal buffer and NOT flush into the IWriter.
    int WriteWithoutFlush(const Record&);

    // Serialize the record into internal buffer and flush into the IWriter.
    int Write(const Record&);

    // Flush internal buffer into the IWriter.
    // Returns 0 on success, error code otherwise.
    int Flush();

private:
    IOBuf _buf;
    IWriter* _writer;
};

} // namespace butil

#endif  // BUTIL_RECORDIO_H
