// Copyright (c) 2015 Baidu, Inc.
// Author: Lin Jiang (jianglin05@baidu.com)
// Date: 2015/7/21 16:44:42

#ifndef RAPIDJSON_OPTIMIZED_WRITER_H
#define RAPIDJSON_OPTIMIZED_WRITER_H

#include "writer.h"

RAPIDJSON_NAMESPACE_BEGIN

//! Optimized writer
/*! This class mainly inherit writer class in rapidjson
 *  It optimised WriteString function in writer class,
 *  which not compare and push character one by one and
 *  in applications when SourceEncoding and TargetEncoding is the same method,
 *  transcode could be omit.
 *  When TargetEncoding support unicode, and SourceEncoding and TargetEncoding
 *  is the same method, WriteString could improve 65% effciency compare with
 *  writer class in rapidjson.
 *  */
template<typename OutputStream, typename SourceEncoding = UTF8<>, 
         typename TargetEncoding = UTF8<>, typename StackAllocator = CrtAllocator>
class OptimizedWriter : 
    public Writer<OutputStream, SourceEncoding, TargetEncoding, StackAllocator> {
public:
    typedef Writer<OutputStream, SourceEncoding, TargetEncoding, StackAllocator> Base; 
    typedef typename SourceEncoding::Ch Ch;
    explicit
    OptimizedWriter(OutputStream& os, StackAllocator* stackAllocator = 0, 
                   size_t levelDepth = Base::kDefaultLevelDepth) : 
        Base(os, stackAllocator, levelDepth) {} 
    
    explicit
    OptimizedWriter(StackAllocator* allocator = 0, 
                   size_t levelDepth = Base::kDefaultLevelDepth) :
        Base(allocator, levelDepth) {} 
    
    bool String(const Ch* str, SizeType length, bool copy = false) {
        (void)copy;
        Base::Prefix(kStringType);
        return WriteString(str, length);
    }

protected:
    bool WriteString(const Ch* str, SizeType length)  {
        //if TargetEncoding support Unicode 
        //and SourceEncoding and TargetEncoding are the same type 
        //just use memcpy to improve efficiency
        if (TargetEncoding::supportUnicode && is_same<SourceEncoding, TargetEncoding>::value) {
            static const char hexDigits[16] = { '0', '1', '2', '3', '4', '5', '6', '7', 
                                                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
            static const char escape[256] = {
#define ESCAPE_ZERO_16 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
                //0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
                'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'b', 't', 'n', 'u', 'f', 'r', 'u', 'u', // 00
                'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', // 10
                  0,   0, '"',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 20
                ESCAPE_ZERO_16, ESCAPE_ZERO_16,                                                 // 30~4F
                  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, '\\',   0,   0,  0, // 50
                ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16, 
                ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16, ESCAPE_ZERO_16  // 60~FF
#undef ESCAPE_ZERO_16
            };
            Base::os_->Put('\"');
            size_t index = 0;
            size_t pos = 0;
            while (pos < length) {
                Ch c = str[pos];
                if ((sizeof(Ch) == 1 || (unsigned)c < 256) && escape[(unsigned char)c]) {
                    Base::os_->Puts(str + index, pos - index);
                    index = pos + 1;
                    Base::os_->Put('\\');
                    Base::os_->Put(escape[(unsigned char)str[pos]]);
                    if (escape[(unsigned char)str[pos]] == 'u') {
                        Base::os_->Put('0');
                        Base::os_->Put('0');
                        Base::os_->Put(hexDigits[(unsigned char)str[pos] >> 4]);
                        Base::os_->Put(hexDigits[(unsigned char)str[pos] & 0xF]);
                    }
                }
                pos++;
            }
            if (index < length) {
                Base::os_->Puts(str + index, length - index);
            }
            Base::os_->Put('\"');
            return true;
        } else { 
            return Base::WriteString(str, length);
        }
    }

private:
    // Prohibit copy constructor & assignment operator.
    OptimizedWriter(const OptimizedWriter&);
    OptimizedWriter& operator=(const OptimizedWriter&);
};
RAPIDJSON_NAMESPACE_END

#endif // RAPIDJSON_OPTIMIZED_WRITER_H
