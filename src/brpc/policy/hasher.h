// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/06/11 17:46:26

#ifndef  BRPC_HASHER_H
#define  BRPC_HASHER_H

#include <stddef.h>
#include <stdint.h>
#include "base/strings/string_piece.h"


namespace brpc {
namespace policy {

uint32_t MD5Hash32(const void* key, size_t len);
uint32_t MD5Hash32V(const base::StringPiece* keys, size_t num_keys);

uint32_t MurmurHash32(const void* key, size_t len);
uint32_t MurmurHash32V(const base::StringPiece* keys, size_t num_keys);

}  // namespace policy
} // namespace brpc


#endif  //BRPC_HASHER_H
