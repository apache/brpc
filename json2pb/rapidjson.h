// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/03/17 15:34:52

#ifndef  PROTOBUF_JSON_RAPIDJSON_H
#define  PROTOBUF_JSON_RAPIDJSON_H


#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)

#pragma GCC diagnostic push

#pragma GCC diagnostic ignored "-Wunused-local-typedefs"

#endif

#include "base/third_party/rapidjson/allocators.h"
#include "base/third_party/rapidjson/document.h"
#include "base/third_party/rapidjson/encodedstream.h"
#include "base/third_party/rapidjson/encodings.h"
#include "base/third_party/rapidjson/filereadstream.h"
#include "base/third_party/rapidjson/filewritestream.h"
#include "base/third_party/rapidjson/prettywriter.h"
#include "base/third_party/rapidjson/rapidjson.h"
#include "base/third_party/rapidjson/reader.h"
#include "base/third_party/rapidjson/stringbuffer.h"
#include "base/third_party/rapidjson/writer.h"
#include "base/third_party/rapidjson/optimized_writer.h"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
#pragma GCC diagnostic pop
#endif

#endif  //PROTOBUF_JSON_RAPIDJSON_H
