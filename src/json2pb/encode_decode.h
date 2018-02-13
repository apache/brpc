// Copyright (c) 2015 Baidu, Inc.
// Author: Jiang,Lin (jianglin05@baidu.com)
// Date: 2015/05/26 16:59:16
#ifndef BRPC_JSON2PB_ENCODE_DECODE_H
#define BRPC_JSON2PB_ENCODE_DECODE_H

namespace json2pb {

//pattern: _Zxxx_
//rules: keep original lower-case characters, upper-case characters,
//digital charactors and '_' in the original position, 
//change other special characters to '_Zxxx_', 
//xxx is the character's decimal digit
//fg: 'abc123_ABC-' convert to 'abc123_ABC_Z045_'

//params: content: content need to encode
//params: encoded_content: content encoded
//return value: false: no need to encode, true: need to encode.
//note: when return value is false, no change in encoded_content.
bool encode_name(const std::string& content, std::string & encoded_content); 

//params: content: content need to decode
//params: decoded_content: content decoded
//return value: false: no need to decode, true: need to decode.
//note: when return value is false, no change in decoded_content.
bool decode_name(const std::string& content, std::string& decoded_content);

}

#endif  //BRPC_JSON2PB_ENCODE_DECODE_H
