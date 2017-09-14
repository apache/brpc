// Copyright (c) 2016 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_AMF_H
#define BRPC_AMF_H

#include <map>
#include <deque>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/message.h>
#include "butil/sys_byteorder.h"
#include "butil/strings/string_piece.h"


namespace brpc {

// For parsing and serializing Action Message Format used throughout RTMP.

// Buffer ZeroCopyInputStream as efficient input of parsing AMF.
class AMFInputStream {
public:
    AMFInputStream(google::protobuf::io::ZeroCopyInputStream* stream)
        : _good(true)
        , _size(0)
        , _data(NULL)
        , _zc_stream(stream)
        , _popped_bytes(0)
    {}

    ~AMFInputStream() { }

    // Cut off at-most n bytes from front side and copy to `out'.
    // Returns bytes cut.
    size_t cutn(void* out, size_t n);

    size_t cut_u8(uint8_t* u16);
    size_t cut_u16(uint16_t* u16);
    size_t cut_u32(uint32_t* u32);
    size_t cut_u64(uint64_t* u64);

    // Returns bytes popped and cut since creation of this stream.
    size_t popped_bytes() const { return _popped_bytes; }

    // Returns false if error occurred in other consuming functions.
    bool good() const { return _good; }
    
    // If the error prevents parsing from going on, call this method.
    // This method is also called in other functions in this class.
    void set_bad() { _good = false; }

    // Return true if the stream is empty. Notice that this function
    // may update _data and _size.
    bool check_emptiness();
    
private:
    bool _good;
    int _size;
    const void* _data;
    google::protobuf::io::ZeroCopyInputStream* _zc_stream;
    size_t _popped_bytes;
};

// Buffer serialize data of AMF to ZeroCopyOutputStream
class AMFOutputStream {
public:
    AMFOutputStream(google::protobuf::io::ZeroCopyOutputStream* stream)
        : _good(true)
        , _size(0)
        , _data(NULL)
        , _zc_stream(stream)
        , _pushed_bytes(0)
    {}

    ~AMFOutputStream() { done(); }

    // Append n bytes.
    void putn(const void* data, int n);

    void put_u8(uint8_t u16);
    void put_u16(uint16_t u16);
    void put_u32(uint32_t u32);
    void put_u64(uint64_t u64);

    // Returns bytes pushed and cut since creation of this stream.
    size_t pushed_bytes() const { return _pushed_bytes; }

    // Returns false if error occurred during serialization.
    bool good() { return _good; }

    void set_bad() { _good = false; }

    // Optionally called to backup buffered bytes to zero-copy stream.
    void done();
    
private:
    bool _good;
    int _size;
    void* _data;
    google::protobuf::io::ZeroCopyOutputStream* _zc_stream;
    size_t _pushed_bytes;
};

// There are 16 core type markers in AMF 0. A type marker is one byte in
// length and describes the kind of encoded data that may follow.
enum AMFMarker {
    AMF_MARKER_NUMBER         = 0x00,
    AMF_MARKER_BOOLEAN        = 0x01,
    AMF_MARKER_STRING         = 0x02,
    AMF_MARKER_OBJECT         = 0x03,
    AMF_MARKER_MOVIECLIP      = 0x04,
    AMF_MARKER_NULL           = 0x05,
    AMF_MARKER_UNDEFINED      = 0x06,
    AMF_MARKER_REFERENCE      = 0x07,
    AMF_MARKER_ECMA_ARRAY     = 0x08,
    AMF_MARKER_OBJECT_END     = 0x09,
    AMF_MARKER_STRICT_ARRAY   = 0x0A,
    AMF_MARKER_DATE           = 0x0B,
    AMF_MARKER_LONG_STRING    = 0x0C,
    AMF_MARKER_UNSUPPORTED    = 0x0D,
    AMF_MARKER_RECORDSET      = 0x0E,
    AMF_MARKER_XML_DOCUMENT   = 0x0F,
    AMF_MARKER_TYPED_OBJECT   = 0x10,
    AMF_MARKER_AVMPLUS_OBJECT = 0x11
};

const char* marker2str(AMFMarker marker);
const char* marker2str(uint8_t marker);

class AMFObject;
class AMFArray;

// A field inside a AMF object.
class AMFField {
friend class AMFObject;
public:
    static const size_t SSO_LIMIT = 8;

    AMFField();
    AMFField(const AMFField&);
    AMFField& operator=(const AMFField&);
    ~AMFField() { Clear(); }
    void Clear() { if (_type != AMF_MARKER_UNDEFINED) { SlowerClear(); } }

    AMFMarker type() const { return (AMFMarker)_type; }
    
    bool IsString() const
    { return _type == AMF_MARKER_STRING || _type == AMF_MARKER_LONG_STRING; }
    bool IsBool() const { return _type == AMF_MARKER_BOOLEAN; }
    bool IsNumber() const { return _type == AMF_MARKER_NUMBER; }
    bool IsObject() const
    { return _type == AMF_MARKER_OBJECT || _type == AMF_MARKER_ECMA_ARRAY; }
    bool IsArray() const { return _type == AMF_MARKER_STRICT_ARRAY; }
    
    butil::StringPiece AsString() const
    { return butil::StringPiece((_is_shortstr ? _shortstr : _str), _strsize); }
    bool AsBool() const { return _b; }
    double AsNumber() const { return _num; }
    const AMFObject& AsObject() const { return *_obj; }
    const AMFArray& AsArray() const { return *_arr; }

    void SetString(const butil::StringPiece& str);
    void SetBool(bool val);
    void SetNumber(double val);
    void SetNull();
    void SetUndefined();
    void SetUnsupported();
    AMFObject* MutableObject();
    AMFArray* MutableArray();
        
private:
    void SlowerClear();

    uint8_t _type;
    bool _is_shortstr;
    uint32_t _strsize;
    union {
        double _num;
        char* _str;
        char _shortstr[SSO_LIMIT]; // SSO
        bool _b;
        AMFObject* _obj;
        AMFArray* _arr;
    };
};
std::ostream& operator<<(std::ostream& os, const AMFField& field);

// A general AMF object.
class AMFObject {
public:
    typedef std::map<std::string, AMFField>::iterator iterator;
    typedef std::map<std::string, AMFField>::const_iterator const_iterator;

    const AMFField* Find(const char* name) const;
    void Remove(const std::string& name) { _fields.erase(name); }
    void Clear() { _fields.clear(); }
    
    void SetString(const std::string& name, const butil::StringPiece& val);
    void SetBool(const std::string& name, bool val);
    void SetNumber(const std::string& name, double val);
    void SetNull(const std::string& name);
    void SetUndefined(const std::string& name);
    void SetUnsupported(const std::string& name);
    AMFObject* MutableObject(const std::string& name);
    AMFArray* MutableArray(const std::string& name);

    iterator begin() { return _fields.begin(); }
    const_iterator begin() const { return _fields.begin(); }
    iterator end() { return _fields.end(); }
    const_iterator end() const { return _fields.end(); }

private:
    std::map<std::string, AMFField> _fields;
};
std::ostream& operator<<(std::ostream& os, const AMFObject&);

// An AMF strict array (not ecma array)
class AMFArray {
public:
    AMFArray();
    AMFArray(const AMFArray&);
    AMFArray& operator=(const AMFArray&);
    ~AMFArray() { Clear(); }
    void Clear();

    const AMFField& operator[](size_t index) const;
    AMFField& operator[](size_t index);
    size_t size() const { return _size; }

    void AddString(const butil::StringPiece& val) { AddField()->SetString(val); }
    void AddBool(bool val) { AddField()->SetBool(val); }
    void AddNumber(double val) { AddField()->SetNumber(val); }
    void AddNull() { AddField()->SetNull(); }
    void AddUndefined() { AddField()->SetUndefined(); }
    void AddUnsupported() { AddField()->SetUnsupported(); }
    AMFObject* AddObject() { return AddField()->MutableObject(); }
    AMFArray* AddArray() { return AddField()->MutableArray(); }

private:
    AMFField* AddField();
    void RemoveLastField();
    
    uint32_t _size;
    AMFField _fields[4];
    std::deque<AMFField> _morefields;
};
std::ostream& operator<<(std::ostream& os, const AMFArray&);

inline const AMFField& AMFArray::operator[](size_t index) const {
    return (index < arraysize(_fields) ? _fields[index] :
            _morefields[index - arraysize(_fields)]);
}
inline AMFField& AMFArray::operator[](size_t index) {
    return (index < arraysize(_fields) ? _fields[index] :
            _morefields[index - arraysize(_fields)]);
}

// Parse types of the stream.
bool ReadAMFString(std::string* val, AMFInputStream* stream);
bool ReadAMFBool(bool* val, AMFInputStream* stream);
bool ReadAMFNumber(double* val, AMFInputStream* stream);
bool ReadAMFUint32(uint32_t* val, AMFInputStream* stream);
bool ReadAMFNull(AMFInputStream* stream);
bool ReadAMFUndefined(AMFInputStream* stream);
bool ReadAMFUnsupported(AMFInputStream* stream);
// The pb version just loads known fields (defined in proto)
bool ReadAMFObject(google::protobuf::Message* msg, AMFInputStream* stream);
bool ReadAMFObject(AMFObject* obj, AMFInputStream* stream);
bool ReadAMFArray(AMFArray* arr, AMFInputStream* stream);

// Serialize types into the stream.
// Check stream->good() for successfulness after one or multiple WriteAMFxxx.
void WriteAMFString(const butil::StringPiece& val, AMFOutputStream* stream);
void WriteAMFBool(bool val, AMFOutputStream* stream);
void WriteAMFNumber(double val, AMFOutputStream* stream);
void WriteAMFUint32(uint32_t val, AMFOutputStream* stream);
void WriteAMFNull(AMFOutputStream* stream);
void WriteAMFUndefined(AMFOutputStream* stream);
void WriteAMFUnsupported(AMFOutputStream* stream);
void WriteAMFObject(const google::protobuf::Message& msg,
                    AMFOutputStream* stream);
void WriteAMFObject(const AMFObject& obj, AMFOutputStream* stream);
void WriteAMFArray(const AMFArray& arr, AMFOutputStream* stream);

} // namespace brpc


#include "brpc/amf_inl.h"

#endif  // BRPC_AMF_H
