// Copyright (c) 2015 Baidu, Inc.
// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/03/17 15:34:52

#ifndef  BRPC_JSON2PB_RAPIDJSON_H
#define  BRPC_JSON2PB_RAPIDJSON_H


#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)

#pragma GCC diagnostic push

#pragma GCC diagnostic ignored "-Wunused-local-typedefs"

#endif

#include "butil/third_party/rapidjson/allocators.h"
#include "butil/third_party/rapidjson/document.h"
#include "butil/third_party/rapidjson/encodedstream.h"
#include "butil/third_party/rapidjson/encodings.h"
#include "butil/third_party/rapidjson/filereadstream.h"
#include "butil/third_party/rapidjson/filewritestream.h"
#include "butil/third_party/rapidjson/prettywriter.h"
#include "butil/third_party/rapidjson/rapidjson.h"
#include "butil/third_party/rapidjson/reader.h"
#include "butil/third_party/rapidjson/stringbuffer.h"
#include "butil/third_party/rapidjson/writer.h"
#include "butil/third_party/rapidjson/optimized_writer.h"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
#pragma GCC diagnostic pop
#endif

#endif  //BRPC_JSON2PB_RAPIDJSON_H
