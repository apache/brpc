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
