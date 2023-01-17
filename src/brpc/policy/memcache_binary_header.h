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


#ifndef BRPC_MEMCACHE_BINARY_HEADER_H
#define BRPC_MEMCACHE_BINARY_HEADER_H


namespace brpc {
namespace policy {

// https://code.google.com/p/memcached/wiki/BinaryProtocolRevamped

// Definition of the legal "magic" values used in a packet.
// See section 3.1 Magic byte
enum MemcacheMagic {
    MC_MAGIC_REQUEST = 0x80,
    MC_MAGIC_RESPONSE = 0x81
};

// Definition of the data types in the packet
// See section 3.4 Data Types
enum MemcacheBinaryDataType {
    MC_BINARY_RAW_BYTES = 0x00
};

// Definition of the different command opcodes.
// See section 3.3 Command Opcodes
enum MemcacheBinaryCommand {
    MC_BINARY_GET = 0x00,
    MC_BINARY_SET = 0x01,
    MC_BINARY_ADD = 0x02,
    MC_BINARY_REPLACE = 0x03,
    MC_BINARY_DELETE = 0x04,
    MC_BINARY_INCREMENT = 0x05,
    MC_BINARY_DECREMENT = 0x06,
    MC_BINARY_QUIT = 0x07,
    MC_BINARY_FLUSH = 0x08,
    MC_BINARY_GETQ = 0x09,
    MC_BINARY_NOOP = 0x0a,
    MC_BINARY_VERSION = 0x0b,
    MC_BINARY_GETK = 0x0c,
    MC_BINARY_GETKQ = 0x0d,
    MC_BINARY_APPEND = 0x0e,
    MC_BINARY_PREPEND = 0x0f,
    MC_BINARY_STAT = 0x10,
    MC_BINARY_SETQ = 0x11,
    MC_BINARY_ADDQ = 0x12,
    MC_BINARY_REPLACEQ = 0x13,
    MC_BINARY_DELETEQ = 0x14,
    MC_BINARY_INCREMENTQ = 0x15,
    MC_BINARY_DECREMENTQ = 0x16,
    MC_BINARY_QUITQ = 0x17,
    MC_BINARY_FLUSHQ = 0x18,
    MC_BINARY_APPENDQ = 0x19,
    MC_BINARY_PREPENDQ = 0x1a,
    MC_BINARY_TOUCH = 0x1c,
    MC_BINARY_GAT = 0x1d,
    MC_BINARY_GATQ = 0x1e,
    MC_BINARY_GATK = 0x23,
    MC_BINARY_GATKQ = 0x24,

    MC_BINARY_SASL_LIST_MECHS = 0x20,
    MC_BINARY_SASL_AUTH = 0x21,
    MC_BINARY_SASL_STEP = 0x22,
    
    // These commands are used for range operations and exist within
    // this header for use in other projects.  Range operations are
    // not expected to be implemented in the memcached server itself.
    MC_BINARY_RGET      = 0x30,
    MC_BINARY_RSET      = 0x31,
    MC_BINARY_RSETQ     = 0x32,
    MC_BINARY_RAPPEND   = 0x33,
    MC_BINARY_RAPPENDQ  = 0x34,
    MC_BINARY_RPREPEND  = 0x35,
    MC_BINARY_RPREPENDQ = 0x36,
    MC_BINARY_RDELETE   = 0x37,
    MC_BINARY_RDELETEQ  = 0x38,
    MC_BINARY_RINCR     = 0x39,
    MC_BINARY_RINCRQ    = 0x3a,
    MC_BINARY_RDECR     = 0x3b,
    MC_BINARY_RDECRQ    = 0x3c
    // End Range operations
};

struct MemcacheRequestHeader {
    // Magic number identifying the package (See BinaryProtocolRevamped#Magic_Byte)
    uint8_t magic;

    // Command code (See BinaryProtocolRevamped#Command_opcodes)
    uint8_t command;

    // Length in bytes of the text key that follows the command extras
    uint16_t key_length;

    // Length in bytes of the command extras
    uint8_t extras_length;

    // Reserved for future use (See BinaryProtocolRevamped#Data_Type)
    uint8_t data_type;

    // The virtual bucket for this command
    uint16_t vbucket_id;

    // Length in bytes of extra + key + value
    uint32_t total_body_length;
    
    // Will be copied back to you in the response
    uint32_t opaque;

    // Data version check
    uint64_t cas_value;
};

struct MemcacheResponseHeader {
    // Magic number identifying the package (See BinaryProtocolRevamped#Magic_Byte)
    uint8_t magic;

    // Command code (See BinaryProtocolRevamped#Command_opcodes)
    uint8_t command;

    // Length in bytes of the text key that follows the command extras
    uint16_t key_length;

    // Length in bytes of the command extras
    uint8_t extras_length;

    // Reserved for future use (See BinaryProtocolRevamped#Data_Type)
    uint8_t data_type;

    // Status of the response (non-zero on error)
    uint16_t status;

    // Length in bytes of extra + key + value
    uint32_t total_body_length;
    
    // Will be copied back to you in the response
    uint32_t opaque;

    // Data version check
    uint64_t cas_value;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_MEMCACHE_BINARY_HEADER_H
