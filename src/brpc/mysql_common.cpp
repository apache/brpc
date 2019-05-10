// Copyright (c) 2019 Baidu, Inc.
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

// Authors: Yang,Liming (yangliming01@baidu.com)

#include "mysql_common.h"

namespace brpc {

const char* MysqlDefaultCollation = "utf8mb4_general_ci";
const char* MysqlBinaryCollation = "binary";

const char* MysqlFieldTypeToString(MysqlFieldType type) {
    switch (type) {
        case MYSQL_FIELD_TYPE_DECIMAL:
        case MYSQL_FIELD_TYPE_TINY:
            return "tiny";
        case MYSQL_FIELD_TYPE_SHORT:
            return "short";
        case MYSQL_FIELD_TYPE_LONG:
            return "long";
        case MYSQL_FIELD_TYPE_FLOAT:
            return "float";
        case MYSQL_FIELD_TYPE_DOUBLE:
            return "double";
        case MYSQL_FIELD_TYPE_NULL:
            return "null";
        case MYSQL_FIELD_TYPE_TIMESTAMP:
            return "timestamp";
        case MYSQL_FIELD_TYPE_LONGLONG:
            return "longlong";
        case MYSQL_FIELD_TYPE_INT24:
            return "int24";
        case MYSQL_FIELD_TYPE_DATE:
            return "date";
        case MYSQL_FIELD_TYPE_TIME:
            return "time";
        case MYSQL_FIELD_TYPE_DATETIME:
            return "datetime";
        case MYSQL_FIELD_TYPE_YEAR:
            return "year";
        case MYSQL_FIELD_TYPE_NEWDATE:
            return "new date";
        case MYSQL_FIELD_TYPE_VARCHAR:
            return "varchar";
        case MYSQL_FIELD_TYPE_BIT:
            return "bit";
        case MYSQL_FIELD_TYPE_JSON:
            return "json";
        case MYSQL_FIELD_TYPE_NEWDECIMAL:
            return "new decimal";
        case MYSQL_FIELD_TYPE_ENUM:
            return "enum";
        case MYSQL_FIELD_TYPE_SET:
            return "set";
        case MYSQL_FIELD_TYPE_TINY_BLOB:
            return "tiny blob";
        case MYSQL_FIELD_TYPE_MEDIUM_BLOB:
            return "blob";
        case MYSQL_FIELD_TYPE_LONG_BLOB:
            return "long blob";
        case MYSQL_FIELD_TYPE_BLOB:
            return "blob";
        case MYSQL_FIELD_TYPE_VAR_STRING:
            return "var string";
        case MYSQL_FIELD_TYPE_STRING:
            return "string";
        case MYSQL_FIELD_TYPE_GEOMETRY:
            return "geometry";
        default:
            return "Unknown Field Type";
    }
}

}  // namespace brpc
