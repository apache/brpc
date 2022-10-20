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

// Date: Thu Nov 22 13:57:56 CST 2012

#ifndef BUTIL_RECORDIO_H
#define BUTIL_RECORDIO_H

#include "butil/iobuf.h"
#include <memory>

namespace butil {

// 0-or-1 Payload + 0-or-multiple Metas.
// Payload and metas are often serialized form of protobuf messages. As a
// correspondence, the implementation is not optimized for very small blobs,
// which should be batched properly before inserting(e.g. using repeated
// field in pb)
class Record {
public:
    struct NamedMeta {
        std::string name;
        std::shared_ptr<butil::IOBuf> data;
    };

    // Number of metas. Could be 0.
    size_t MetaCount() const { return _metas.size(); }

    // Get i-th Meta, out-of-range accesses may crash.
    // This method is mainly for iterating all metas.
    const NamedMeta& MetaAt(size_t i) const { return _metas[i]; }

    // Get meta by |name|. NULL on not found.
    const butil::IOBuf* Meta(const char* name) const;

    // Returns a mutable pointer to the meta with |name|. If the meta does
    // not exist, add it first.
    // If |null_on_found| is true and meta with |name| is present, NULL is
    // returned. This is useful for detecting uniqueness of meta names in some
    // scenarios.
    // NOTE: With the assumption that there won't be many metas, the impl.
    // tests presence by scaning all fields, which may perform badly when metas
    // are a lot.
    butil::IOBuf* MutableMeta(const char* name, bool null_on_found = false);
    butil::IOBuf* MutableMeta(const std::string& name, bool null_on_found = false);

    // Remove meta with the name. The impl. may scan all fields.
    // Returns true on erased, false on absent.
    bool RemoveMeta(const butil::StringPiece& name);

    // Get the payload. Empty by default.
    const butil::IOBuf& Payload() const { return _payload; }

    // Get a mutable pointer to the payload.
    butil::IOBuf* MutablePayload() { return &_payload; }

    // Clear payload and remove all meta.
    void Clear();

    // Serialized size of this record.
    size_t ByteSize() const;

private:
    butil::IOBuf _payload;
    std::vector<NamedMeta> _metas;
};

// Parse records from the IReader, corrupted records will be skipped.
// Example:
//    RecordReader rd(...);
//    Record rec;
//    while (rd.ReadNext(&rec)) {
//        // Handle the rec
//    }
//    if (rd.last_error() != RecordReader::END_OF_READER) {
//        LOG(FATAL) << "Critical error occurred";
//    }
class RecordReader {
public:
    // A special error code to mark end of input data.
    static const int END_OF_READER = -1;

    explicit RecordReader(IReader* reader);

    // Returns true on success and |out| is overwritten by the record.
    // False otherwise and last_error() is the error which is treated as permanent.
    bool ReadNext(Record* out);

    // 0 means no error.
    // END_OF_READER means all data in the IReader are successfully consumed.
    int last_error() const { return _last_error; }

    // Total bytes consumed.
    // NOTE: this value may not equal to read bytes from the IReader even if
    // the reader runs out, due to parsing errors.
    size_t offset() const { return _ncut; }

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

    // Serialize |record| into internal buffer and NOT flush into the IWriter.
    int WriteWithoutFlush(const Record& record);

    // Serialize |record| into internal buffer and flush into the IWriter.
    int Write(const Record& record);

    // Flush internal buffer into the IWriter.
    // Returns 0 on success, error code otherwise.
    int Flush();

private:
    IOBuf _buf;
    IWriter* _writer;
};

} // namespace butil

#endif  // BUTIL_RECORDIO_H
