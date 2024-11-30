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


#include "brpc/details/hpack.h"

#include <limits>                                       // std::numeric_limits
#include <vector>
#include "butil/containers/bounded_queue.h"              // butil::BoundedQueue
#include "butil/containers/flat_map.h"                   // butil::FlatMap
#include "butil/containers/case_ignored_flat_map.h"      // butil::FlatMap
#include "brpc/details/hpack-static-table.h"       // s_static_headers


namespace brpc {

// Options to initialize a IndexTable
struct IndexTableOptions {
    size_t max_size;
    uint64_t start_index;
    const HeaderCstr* static_table;
    size_t static_table_size;
    bool need_indexes;
    IndexTableOptions()
        : max_size(0)
        , start_index(0)
        , static_table(NULL)
        , static_table_size(0)
        , need_indexes(false)
    {}
};

struct HeaderAndHashCode {
    size_t hash_code;
    const HPacker::Header* header;
};

struct HeaderHasher {
    size_t operator()(const HPacker::Header& h) const {
        return butil::CaseIgnoredHasher()(h.name)
            * 101 + butil::DefaultHasher<std::string>()(h.value);
    }
    size_t operator()(const HeaderAndHashCode& h) const {
        return h.hash_code;
    }
};

struct HeaderEqualTo {
    bool operator()(const HPacker::Header& h1, const HPacker::Header& h2) const {
        return butil::CaseIgnoredEqual()(h1.name, h2.name)
            && butil::DefaultEqualTo<std::string>()(h1.value, h2.value);
    }
    bool operator()(const HPacker::Header& h1, const HeaderAndHashCode& h2) const {
        return operator()(h1, *h2.header);
    }
};

class BAIDU_CACHELINE_ALIGNMENT IndexTable {
DISALLOW_COPY_AND_ASSIGN(IndexTable);
    typedef HPacker::Header Header;
public:
    IndexTable()
        : _start_index(0)
        , _add_times(0)
        , _size(0)
    {}
    ~IndexTable() {}
    int Init(const IndexTableOptions& options);

    const Header* HeaderAt(int index) const {
        if (BAIDU_UNLIKELY(index < _start_index)) {
            return NULL;
        }
        return _header_queue.bottom(index - _start_index);
    };

    int GetIndexOfHeader(const HeaderAndHashCode& h) {
        DCHECK(_need_indexes);
        const uint64_t* v = _header_index.seek(h);
        if (!v) {
            return 0;
        }
        DCHECK_LE(_add_times - *v, _header_queue.size());
        // The latest added entry has the smallest index
        return _start_index + (_add_times - *v) - 1;
    }

    int GetIndexOfName(const std::string& name) {
        DCHECK(_need_indexes);
        const uint64_t* v = _name_index.seek(name);
        if (!v) {
            return 0;
        }
        DCHECK_LE(_add_times - *v, _header_queue.size());
        // The latest added entry has the smallest index
        return _start_index + (_add_times - *v) - 1;
    }

    bool empty() const { return _size == 0; }
    int start_index() const { return _start_index; }
    int end_index() const { return start_index() + _header_queue.size(); }

    static inline size_t HeaderSize(const Header& h) {
        // https://tools.ietf.org/html/rfc7541#section-4.1
        return h.name.size() + h.value.size() + 32;
    }

    void PopHeader() {
        DCHECK(!empty());
        const Header* h = _header_queue.top();
        const size_t entry_size = HeaderSize(*h);
        DCHECK_LE(entry_size, _size);
        const uint64_t id = _add_times - _header_queue.size();
        if (_need_indexes) {
            RemoveHeaderFromIndexes(*h, id);
        }
        _size -= entry_size;
        _header_queue.pop();
    }

    void RemoveHeaderFromIndexes(const Header& h, uint64_t expected_id) {
        if (!h.value.empty()) {
            const uint64_t* v = _header_index.seek(h);
            DCHECK(v);
            if (*v == expected_id) {
                _header_index.erase(h);
            }
        }
        const uint64_t* v = _name_index.seek(h.name);
        DCHECK(v);
        if (*v == expected_id) {
            _name_index.erase(h.name);
        }
    }

    void AddHeader(const Header& h) {
        CHECK(!h.name.empty());
        const size_t entry_size = HeaderSize(h);

        while (!empty() && (_size + entry_size) > _max_size) {
            PopHeader();
        }

        if (entry_size > _max_size) {
            // https://tools.ietf.org/html/rfc7541#section-4.1
            // If this header is larger than the max size, clear the table only.
            DCHECK(empty());
            return;
        }

        _size += entry_size;
        CHECK(!_header_queue.full());
        _header_queue.push(h);

        const int id = _add_times++;
        if (_need_indexes) {
            // Overwrite existance value.
            if (!h.value.empty()) {
                _header_index[h] = id;
            }
            _name_index[h.name] = id;
        }
    }

    void ResetMaxSize(size_t new_max_size) {
        LOG(INFO) << this << ".size=" << _size << " new_max_size=" << new_max_size
                  << " max_size=" << _max_size;
        if (new_max_size > _max_size) {
            //LOG(ERROR) << "Invalid new_max_size=" << new_max_size;
            //return -1;
            _max_size = new_max_size;
            return;
        }
        if (new_max_size < _max_size) {
            _max_size = new_max_size;
            while (_size > _max_size) {
                PopHeader();
            }
        }
        return;
    }

    void Print(std::ostream& os) const;

private:

    int _start_index;
    bool _need_indexes;
    uint64_t _add_times;  // Increase when adding a new entry.
    size_t _max_size;
    size_t _size;
    butil::BoundedQueue<Header> _header_queue;

    // -----------------------  Encoder only ----------------------------
    // Indexes that map entry to the latest time it was added.
    // Note that duplicated entries are allowed in index table, which indicates
    // that the same header is possibly added/removed for multiple times,
    // requiring a costly multimap to index all the header entries.
    // Since the encoder just cares whether this header is in the index table
    // rather than which the index number is, only the latest entry of the same
    // header is indexed here, which is definitely the last one to be removed.
    butil::FlatMap<Header, uint64_t, HeaderHasher, HeaderEqualTo> _header_index;
    butil::CaseIgnoredFlatMap<uint64_t> _name_index;
};

int IndexTable::Init(const IndexTableOptions& options) {
    size_t num_headers = 0;
    if (options.static_table_size > 0) {
        num_headers = options.static_table_size;
        _max_size = UINT_MAX;
    } else {
        num_headers = options.max_size / (32 + 2);
        //                                     ^
        // name and value both have at least one byte in.
        _max_size = options.max_size;
    }
    void *header_queue_storage = malloc(num_headers * sizeof(Header));
    if (!header_queue_storage) {
        LOG(ERROR) << "Fail to malloc space for " << num_headers << " headers";
        return -1;
    }
    butil::BoundedQueue<Header> tmp(
        header_queue_storage, num_headers * sizeof(Header),
        butil::OWNS_STORAGE);
    _header_queue.swap(tmp);
    _start_index = options.start_index;
    _need_indexes = options.need_indexes;
    if (_need_indexes) {
        if (_name_index.init(num_headers * 2) != 0) {
            LOG(WARNING) << "Fail to init _name_index";
        }
        if (_header_index.init(num_headers * 2) != 0) {
            LOG(WARNING) << "Fail to init _name_index";
        }
    }
    if (options.static_table_size > 0) {
        // Add header in the reverse order
        for (int i = options.static_table_size - 1; i >= 0; --i) {
            Header h;
            h.name = options.static_table[i].name;
            h.value = options.static_table[i].value;
            AddHeader(h);
        }
    }
    return 0;
}

void IndexTable::Print(std::ostream& os) const {
    os << "{start_index=" << _start_index
       << " need_indexes=" << _need_indexes
       << " add_times=" << _add_times
       << " max_size=" << _max_size
       << " size=" << _size
       << " header_queue.size=" << _header_queue.size()
       << " header_index.size=" << _header_index.size()
       << " name_index.size=" << _name_index.size()
       << '}';
}

struct HuffmanNode {
    uint16_t left_child;
    uint16_t right_child;
    int32_t value;
};

class BAIDU_CACHELINE_ALIGNMENT HuffmanTree {
DISALLOW_COPY_AND_ASSIGN(HuffmanTree);
public:
    typedef uint16_t NodeId;
    enum ConstValue {
        NULL_NODE = 0,
        ROOT_NODE = 1,
        INVALID_VALUE = INT_MAX
    };

    HuffmanTree() {
        // Allocate memory for root
        HuffmanNode root = { NULL_NODE, NULL_NODE, INVALID_VALUE };
        _node_memory.push_back(root);
    }

    void AddLeafNode(int32_t value, const HuffmanCode& code) {
        NodeId cur = ROOT_NODE;
        for (int i = code.bit_len; i > 0; i--) {
            CHECK_EQ(node(cur).value, INVALID_VALUE) << "value=" << value << "cur=" << cur;
            if (code.code & (1u << (i - 1))) {
                if (node(cur).right_child == NULL_NODE) {
                    NodeId new_id = AllocNode();
                    node(cur).right_child = new_id;
                }
                cur = node(cur).right_child;
            } else {
                if (node(cur).left_child == NULL_NODE) {
                    NodeId new_id = AllocNode();
                    node(cur).left_child = new_id;
                }
                cur = node(cur).left_child;
            }
        }
        CHECK_EQ(INVALID_VALUE, node(cur).value) << "value=" << value << " cur=" << cur;
        CHECK_EQ(NULL_NODE, node(cur).left_child);
        CHECK_EQ(NULL_NODE, node(cur).right_child);
        node(cur).value = value;
    }

    const HuffmanNode* node(NodeId id) const {
        if (id == 0u) {
            return NULL;
        }
        if (id > _node_memory.size()) {
            return NULL;
        }
        return &_node_memory[id - 1];
    }

private:

    HuffmanNode& node(NodeId id) {
        return _node_memory[id - 1];
    }

    NodeId AllocNode() {
        const NodeId id = _node_memory.size() + 1;
        HuffmanNode node = { NULL_NODE, NULL_NODE, INVALID_VALUE };
        _node_memory.push_back(node);
        return id;
    }
    std::vector<HuffmanNode> _node_memory;

};

class HuffmanEncoder {
DISALLOW_COPY_AND_ASSIGN(HuffmanEncoder);
public:
    HuffmanEncoder(butil::IOBufAppender* out, const HuffmanCode* table)
        : _out(out)
        , _table(table)
        , _partial_byte(0)
        , _remain_bit(8)
        , _out_bytes(0)
    {}

    void Encode(unsigned char byte) {
        const HuffmanCode code = _table[byte];
        uint16_t bits_left = code.bit_len;
        while (bits_left) {
            const uint16_t adding_bits_len = std::min(_remain_bit, bits_left);
            const uint8_t adding_bits = static_cast<uint8_t>(
                    (code.code & ((1u << bits_left) - 1))   // clear leading bits
                            >> (bits_left - adding_bits_len));  // align to LSB
            _partial_byte |= adding_bits << (_remain_bit - adding_bits_len);
            _remain_bit -= adding_bits_len;
            bits_left -= adding_bits_len;
            if (!_remain_bit) {
                ++_out_bytes;
                _out->push_back(_partial_byte);
                _remain_bit = 8;
                _partial_byte = 0;
            }
        }
    }

    void EndStream() {
        if (_remain_bit == 8u) {
            return;
        }
        DCHECK_LT(_remain_bit, 8u);
        // Add padding `1's to lsb to make _out aligned
        _partial_byte |= (1 << _remain_bit) - 1;
        // TODO: push_back is probably costly since it acquires tls everytime it
        // is invoked.
        _out->push_back(_partial_byte);
        _partial_byte = 0;
        _remain_bit = 0;
        _out = NULL;
        ++_out_bytes;
    }

    uint32_t out_bytes() const { return _out_bytes; }

private:
    butil::IOBufAppender* _out;
    const HuffmanCode* _table;
    uint8_t  _partial_byte;
    uint16_t _remain_bit;
    uint32_t _out_bytes;
};

class HuffmanDecoder {
DISALLOW_COPY_AND_ASSIGN(HuffmanDecoder);
public:
    HuffmanDecoder(std::string* out, const HuffmanTree* tree)
        // FIXME: resizing of out is costly
        : _out(out)
        , _tree(tree)
        , _cur_node(tree->node(HuffmanTree::ROOT_NODE))
        , _cur_depth(0)  // Depth of root node is 0
        , _padding(true)
    {}
    int Decode(uint8_t byte) {
        for (int i = 7; i >= 0; --i) {
            if (byte & (1u << i)) {
                _cur_node = _tree->node(_cur_node->right_child);
                if (BAIDU_UNLIKELY(!_cur_node)) {
                    LOG(ERROR) << "Decoder stream reaches NULL_NODE";
                    return -1;
                }
                if (_cur_node->value != HuffmanTree::INVALID_VALUE) {
                    if (BAIDU_UNLIKELY(_cur_node->value == HPACK_HUFFMAN_EOS)) {
                        LOG(ERROR) << "Decoder stream reaches EOS";
                        return -1;
                    }
                    _out->push_back(static_cast<uint8_t>(_cur_node->value));
                    _cur_node = _tree->node(HuffmanTree::ROOT_NODE);
                    _cur_depth = 0;
                    _padding = true;
                    continue;
                }
                _padding &= 1;
            } else {
                _cur_node = _tree->node(_cur_node->left_child);
                if (BAIDU_UNLIKELY(!_cur_node)) {
                    LOG(ERROR) << "Decoder stream reaches NULL_NODE";
                    return -1;
                }
                if (_cur_node->value != HuffmanTree::INVALID_VALUE) {
                    if (BAIDU_UNLIKELY(_cur_node->value == HPACK_HUFFMAN_EOS)) {
                        LOG(ERROR) << "Decoder stream reaches EOS";
                        return -1;
                    }
                    _out->push_back(static_cast<uint8_t>(_cur_node->value));
                    _cur_node = _tree->node(HuffmanTree::ROOT_NODE);
                    _cur_depth = 0;
                    _padding = true;
                    continue;
                }
                _padding &= 0;
            }
            ++_cur_depth;
        }
        return 0;
    }
    int EndStream() {
        if (_cur_depth == 0) {
            return 0;
        }
        if (_cur_depth <= 7 && _padding) {
            return 0;
        }
        // Invalid stream, the padding is not corresponding to MSB of EOS
        // https://tools.ietf.org/html/rfc7541#section-5.2
        return -1;
    }
private:
    std::string* _out;
    const HuffmanTree* _tree;
    const HuffmanNode* _cur_node;
    uint16_t _cur_depth;
    bool _padding;
};

// Primitive Type Representations

// Encode variant intger and return the size
inline void EncodeInteger(butil::IOBufAppender* out, uint8_t msb,
                          uint8_t prefix_size, uint32_t value) {
    uint8_t max_prefix_value = (1 << prefix_size) - 1;
    if (value < max_prefix_value) {
        msb |= value;
        out->push_back(msb);
        return;
    }
    value -= max_prefix_value;
    msb |= max_prefix_value;
    out->push_back(msb);
    for (; value >= 128; ) {
        const uint8_t c = (value & 0x7f) | 0x80;
        value >>= 7;
        out->push_back(c);
    }
    out->push_back(static_cast<uint8_t>(value));
}

// Static variables
static HuffmanTree* s_huffman_tree = NULL;
static IndexTable* s_static_table = NULL;
static pthread_once_t s_create_once = PTHREAD_ONCE_INIT;

static void CreateStaticTableOrDie() {
    s_huffman_tree = new HuffmanTree;
    for (size_t i = 0; i < ARRAY_SIZE(s_huffman_table); ++i) {
        s_huffman_tree->AddLeafNode(i, s_huffman_table[i]);
    }
    IndexTableOptions options;
    options.max_size = UINT_MAX;
    options.static_table = s_static_headers;
    options.static_table_size = ARRAY_SIZE(s_static_headers);
    options.start_index = 1;
    options.need_indexes = true;
    s_static_table = new IndexTable;
    if (s_static_table->Init(options) != 0) {
        LOG(ERROR) << "Fail to init static table";
        exit(1);
    }
}

static void CreateStaticTableOnceOrDie() {
    if (pthread_once(&s_create_once, CreateStaticTableOrDie) != 0) {
        PLOG(ERROR) << "Fail to pthread_once";
        exit(1);
    }
}

// Assume that no header would be larger than 10MB
static const size_t MAX_HPACK_INTEGER = 10 * 1024 * 1024ul;

inline ssize_t DecodeInteger(butil::IOBufBytesIterator& iter,
                             uint8_t prefix_size, uint32_t* value) {
    if (iter == NULL) {
        return 0; // No enough data
    }
    uint8_t first_byte = *iter;
    uint64_t tmp = first_byte & ((1 << prefix_size) - 1);
    ++iter;
    if (tmp < ((1u << prefix_size) - 1)) {
        *value = static_cast<uint32_t>(tmp);
        return 1;
    }
    uint8_t cur_byte = 0;
    int m = 0;
    ssize_t in_bytes = 1;
    do {
        if (!iter) {
            return 0;
        }
        cur_byte = *iter;
        in_bytes++;
        tmp += static_cast<uint64_t>(cur_byte & 0x7f) << m;
        m += 7;
        ++iter;
    } while ((cur_byte & 0x80) && (tmp < MAX_HPACK_INTEGER));

    if (tmp >= MAX_HPACK_INTEGER) {
        LOG(ERROR) << "Source stream is likely malformed";
        return -1;
    }

    *value = static_cast<uint32_t>(tmp);

    return in_bytes;
}

template <bool LOWERCASE> // use template to remove dead branches.
inline void EncodeString(butil::IOBufAppender* out, const std::string& s,
                         bool huffman_encoding) {
    if (!huffman_encoding) {
        EncodeInteger(out, 0x00, 7, s.size());
        if (LOWERCASE) {
            for (size_t i = 0; i < s.size(); ++i) {
                out->push_back(butil::ascii_tolower(s[i]));
            }
        } else {
            out->append(s);
        }
        return;
    }
    // Calculate length of encoded string
    uint32_t bit_len = 0;
    if (LOWERCASE) {
        for (size_t i = 0; i < s.size(); ++i) {
            bit_len += s_huffman_table[(uint8_t)butil::ascii_tolower(s[i])].bit_len;
        }
    } else {
        for (size_t i = 0; i < s.size(); ++i) {
            bit_len += s_huffman_table[(uint8_t)s[i]].bit_len;
        }
    }
    EncodeInteger(out, 0x80, 7, (bit_len >> 3) + !!(bit_len & 7));
    HuffmanEncoder e(out, s_huffman_table);
    if (LOWERCASE) {
        for (size_t i = 0; i < s.size(); ++i) {
            e.Encode(butil::ascii_tolower(s[i]));
        }
    } else {
        for (size_t i = 0; i < s.size(); ++i) {
            e.Encode(s[i]);
        }
    }
    e.EndStream();
}

inline ssize_t DecodeString(butil::IOBufBytesIterator& iter, std::string* out) {
    if (iter == NULL) {
        return 0;
    }
    const bool huffman = *iter & 0x80;
    uint32_t length = 0;
    ssize_t in_bytes = DecodeInteger(iter, 7, &length);
    if (in_bytes <= 0) {
        return -1;
    }
    if (length > iter.bytes_left()) {
        return 0;
    }
    in_bytes += length;
    out->clear();
    if (!huffman) {
        iter.copy_and_forward(out, length);
        return in_bytes;
    }
    HuffmanDecoder d(out, s_huffman_tree);
    for (; iter != NULL && length; ++iter, --length) {
        if (d.Decode(*iter) != 0) {
            return -1;
        }
    }
    if (d.EndStream() != 0) {
        return -1;
    }
    return in_bytes;
}

HPacker::HPacker()
    : _encode_table(NULL)
    , _decode_table(NULL) {
    CreateStaticTableOnceOrDie();
}

HPacker::~HPacker() {
    if (_encode_table) {
        delete _encode_table;
        _encode_table = NULL;
    }
    if (_decode_table) {
        delete _decode_table;
        _decode_table = NULL;
    }
}

int HPacker::Init(size_t max_table_size) {
    CHECK(!_encode_table);
    CHECK(!_decode_table);
    IndexTableOptions encode_table_options;
    encode_table_options.max_size = max_table_size;
    encode_table_options.start_index = s_static_table->end_index();
    encode_table_options.need_indexes = true;
    _encode_table = new IndexTable;
    if (_encode_table->Init(encode_table_options) != 0) {
        LOG(ERROR) << "Fail to init encode table";
        return -1;
    }
    IndexTableOptions decode_table_options;
    decode_table_options.max_size = max_table_size;
    decode_table_options.start_index = s_static_table->end_index();
    decode_table_options.need_indexes = false;
    _decode_table = new IndexTable;
    if (_decode_table->Init(decode_table_options) != 0) {
        LOG(ERROR) << "Fail to init decode table";
        return -1;
    }
    return 0;
}

inline int HPacker::FindHeaderFromIndexTable(const Header& h) const {
    // saves a hash (which is a hotspot) for ones missing s_static_table
    const HeaderAndHashCode hhc = { HeaderHasher()(h), &h };
    int index = s_static_table->GetIndexOfHeader(hhc);
    if (index > 0) {
        return index;
    }
    return _encode_table->GetIndexOfHeader(hhc);
}

inline int HPacker::FindNameFromIndexTable(const std::string& name) const {
    int index = s_static_table->GetIndexOfName(name);
    if (index > 0) {
        return index;
    }
    return _encode_table->GetIndexOfName(name);
}

void HPacker::Encode(butil::IOBufAppender* out, const Header& header,
                     const HPackOptions& options) {
    if (options.index_policy != HPACK_NEVER_INDEX_HEADER) {
        const int index = FindHeaderFromIndexTable(header);
        if (index > 0) {
            // This header is already in the index table
            return EncodeInteger(out, 0x80, 7, index);
        }
    } // The header can't be indexed or the header wasn't in the index table
    
    const int name_index = FindNameFromIndexTable(header.name);
    if (options.index_policy == HPACK_INDEX_HEADER) {
        // TODO: Add Options that indexes name independently
        _encode_table->AddHeader(header);
    }
    switch (options.index_policy) {
    case HPACK_INDEX_HEADER:
        EncodeInteger(out, 0x40, 6, name_index);
        break;
    case HPACK_NOT_INDEX_HEADER:
        EncodeInteger(out, 0x00, 4, name_index);
        break;
    case HPACK_NEVER_INDEX_HEADER:
        EncodeInteger(out, 0x10, 4, name_index);
        break;
    }
    if (name_index == 0) {
        EncodeString<true>(out, header.name, options.encode_name);
    }
    EncodeString<false>(out, header.value, options.encode_value);
}

inline const HPacker::Header* HPacker::HeaderAt(int index) const {
    return (index >= _decode_table->start_index())
            ? _decode_table->HeaderAt(index) : s_static_table->HeaderAt(index);
}

inline ssize_t HPacker::DecodeWithKnownPrefix(
    butil::IOBufBytesIterator& iter, Header* h, uint8_t prefix_size) const {
    int index = 0;
    ssize_t index_bytes = DecodeInteger(iter, prefix_size, (uint32_t*)&index);
    ssize_t name_bytes = 0;
    if (index_bytes <= 0) {
        LOG(ERROR) << "Fail to decode index";
        return -1;
    }
    if (index != 0) {
        const Header* indexed_header = HeaderAt(index);
        if (indexed_header == NULL) {
            LOG(ERROR) << "No header at index=" << index;
            return -1;
        }
        h->name = indexed_header->name;
    } else {
        name_bytes = DecodeString(iter, &h->name);
        if (name_bytes <= 0) {
            LOG(ERROR) << "Fail to decode name";
            return -1;
        }
        tolower(&h->name);
    }
    ssize_t value_bytes = DecodeString(iter, &h->value);
    if (value_bytes <= 0) {
        LOG(ERROR) << "Fail to decode value";
        return -1;
    }
    return index_bytes + name_bytes + value_bytes;
}

ssize_t HPacker::Decode(butil::IOBufBytesIterator& iter, Header* h) {
    if (iter == NULL) {
        return 0;
    }
    const uint8_t first_byte = *iter;
    // Check the leading 4 bits to determin the entry type
    switch (first_byte >> 4) {
    case 15:
    case 14:
    case 13:
    case 12:
    case 11:
    case 10:
    case 9:
    case 8:
        // (1xxx) Indexed Header Field Representation
        // https://tools.ietf.org/html/rfc7541#section-6.1
        {
            int index = 0;
            ssize_t index_bytes = DecodeInteger(iter, 7, (uint32_t*)&index);
            if (index_bytes <= 0) {
                return index_bytes;
            }
            const Header* indexed_header = HeaderAt(index);
            if (indexed_header == NULL) {
                LOG(ERROR) << "No header at index=" << index;
                return -1;
            }
            *h = *indexed_header;
            return index_bytes;
        }
        break;
    case 7:
    case 5:
    case 6:
    case 4:
        // (01xx) Literal Header Field with Incremental Indexing
        // https://tools.ietf.org/html/rfc7541#section-6.2.1
        {
            const ssize_t bytes_consumed = DecodeWithKnownPrefix(iter, h, 6);
            if (bytes_consumed <= 0) {
                return -1;
            }
            _decode_table->AddHeader(*h);
            return bytes_consumed;
        }
        break;
    case 3:
    case 2:
        // (001x) Dynamic Table Size Update
        // https://tools.ietf.org/html/rfc7541#section-6.3
        {
            uint32_t max_size = 0;
            ssize_t read_bytes = DecodeInteger(iter, 5, &max_size);
            if (read_bytes <= 0) {
                return read_bytes;
            }
            if (max_size > H2Settings::DEFAULT_HEADER_TABLE_SIZE) {
                LOG(ERROR) << "Invalid max_size=" << max_size;
                return -1;
            }
            _decode_table->ResetMaxSize(max_size);
            return Decode(iter, h);
        }
    case 1:
        // (0001) Literal Header Field Never Indexed
        // https://tools.ietf.org/html/rfc7541#section-6.2.3
        return DecodeWithKnownPrefix(iter, h, 4);
        // TODO: Expose NeverIndex to the caller.
    case 0:
        // (0000) Literal Header Field without Indexing
        // https://tools.ietf.org/html/rfc7541#section-6.2.1
        return DecodeWithKnownPrefix(iter, h, 4);
        // TODO: Expose NeverIndex to the caller.
    default:
        CHECK(false) << "Can't reach here";
        return -1;
    }
}

void HPacker::Describe(std::ostream& os, const DescribeOptions& opt) const {
    if (opt.verbose) {
        os << '\n';
    }
    const char sep = (opt.verbose ? '\n' : ' ');
    os << "encode_table=";
    if (_encode_table) {
        _encode_table->Print(os);
    } else {
        os << "null";
    }
    os << sep << "decode_table=";
    if (_decode_table) {
        _decode_table->Print(os);
    } else {
        os << "null";
    }
    if (opt.verbose) {
        os << '\n';
    }
}

void tolower(std::string* s) {
    const char* d = s->c_str();
    for (size_t i = 0; i < s->size(); ++i) {
        const char c = d[i];
        const char c2 = butil::ascii_tolower(c);
        if (c2 != c) {
            (*s)[i] = c2;
        }
    }
}

} // namespace brpc
