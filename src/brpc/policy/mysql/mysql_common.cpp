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

// Authors: Yang,Liming (yangliming01@baidu.com)

#include "brpc/policy/mysql/mysql_common.h"

namespace brpc {

// Definition lives here (single TU) to avoid a per-include copy of the map.
// utf16/ucs2/utf32 collations are not supported.
const std::map<std::string, uint8_t> MysqlCollations = {
    {"big5_chinese_ci", 1},
    {"latin2_czech_cs", 2},
    {"dec8_swedish_ci", 3},
    {"cp850_general_ci", 4},
    {"latin1_german1_ci", 5},
    {"hp8_english_ci", 6},
    {"koi8r_general_ci", 7},
    {"latin1_swedish_ci", 8},
    {"latin2_general_ci", 9},
    {"swe7_swedish_ci", 10},
    {"ascii_general_ci", 11},
    {"ujis_japanese_ci", 12},
    {"sjis_japanese_ci", 13},
    {"cp1251_bulgarian_ci", 14},
    {"latin1_danish_ci", 15},
    {"hebrew_general_ci", 16},
    {"tis620_thai_ci", 18},
    {"euckr_korean_ci", 19},
    {"latin7_estonian_cs", 20},
    {"latin2_hungarian_ci", 21},
    {"koi8u_general_ci", 22},
    {"cp1251_ukrainian_ci", 23},
    {"gb2312_chinese_ci", 24},
    {"greek_general_ci", 25},
    {"cp1250_general_ci", 26},
    {"latin2_croatian_ci", 27},
    {"gbk_chinese_ci", 28},
    {"cp1257_lithuanian_ci", 29},
    {"latin5_turkish_ci", 30},
    {"latin1_german2_ci", 31},
    {"armscii8_general_ci", 32},
    {"utf8_general_ci", 33},
    {"cp1250_czech_cs", 34},
    {"cp866_general_ci", 36},
    {"keybcs2_general_ci", 37},
    {"macce_general_ci", 38},
    {"macroman_general_ci", 39},
    {"cp852_general_ci", 40},
    {"latin7_general_ci", 41},
    {"latin7_general_cs", 42},
    {"macce_bin", 43},
    {"cp1250_croatian_ci", 44},
    {"utf8mb4_general_ci", 45},
    {"utf8mb4_bin", 46},
    {"latin1_bin", 47},
    {"latin1_general_ci", 48},
    {"latin1_general_cs", 49},
    {"cp1251_bin", 50},
    {"cp1251_general_ci", 51},
    {"cp1251_general_cs", 52},
    {"macroman_bin", 53},
    {"cp1256_general_ci", 57},
    {"cp1257_bin", 58},
    {"cp1257_general_ci", 59},
    {"binary", 63},
    {"armscii8_bin", 64},
    {"ascii_bin", 65},
    {"cp1250_bin", 66},
    {"cp1256_bin", 67},
    {"cp866_bin", 68},
    {"dec8_bin", 69},
    {"greek_bin", 70},
    {"hebrew_bin", 71},
    {"hp8_bin", 72},
    {"keybcs2_bin", 73},
    {"koi8r_bin", 74},
    {"koi8u_bin", 75},
    {"utf8_tolower_ci", 76},
    {"latin2_bin", 77},
    {"latin5_bin", 78},
    {"latin7_bin", 79},
    {"cp850_bin", 80},
    {"cp852_bin", 81},
    {"swe7_bin", 82},
    {"utf8_bin", 83},
    {"big5_bin", 84},
    {"euckr_bin", 85},
    {"gb2312_bin", 86},
    {"gbk_bin", 87},
    {"sjis_bin", 88},
    {"tis620_bin", 89},
    {"ujis_bin", 91},
    {"geostd8_general_ci", 92},
    {"geostd8_bin", 93},
    {"latin1_spanish_ci", 94},
    {"cp932_japanese_ci", 95},
    {"cp932_bin", 96},
    {"eucjpms_japanese_ci", 97},
    {"eucjpms_bin", 98},
    {"cp1250_polish_ci", 99},
    {"utf8_unicode_ci", 192},
    {"utf8_icelandic_ci", 193},
    {"utf8_latvian_ci", 194},
    {"utf8_romanian_ci", 195},
    {"utf8_slovenian_ci", 196},
    {"utf8_polish_ci", 197},
    {"utf8_estonian_ci", 198},
    {"utf8_spanish_ci", 199},
    {"utf8_swedish_ci", 200},
    {"utf8_turkish_ci", 201},
    {"utf8_czech_ci", 202},
    {"utf8_danish_ci", 203},
    {"utf8_lithuanian_ci", 204},
    {"utf8_slovak_ci", 205},
    {"utf8_spanish2_ci", 206},
    {"utf8_roman_ci", 207},
    {"utf8_persian_ci", 208},
    {"utf8_esperanto_ci", 209},
    {"utf8_hungarian_ci", 210},
    {"utf8_sinhala_ci", 211},
    {"utf8_german2_ci", 212},
    {"utf8_croatian_ci", 213},
    {"utf8_unicode_520_ci", 214},
    {"utf8_vietnamese_ci", 215},
    {"utf8_general_mysql500_ci", 223},
    {"utf8mb4_unicode_ci", 224},
    {"utf8mb4_icelandic_ci", 225},
    {"utf8mb4_latvian_ci", 226},
    {"utf8mb4_romanian_ci", 227},
    {"utf8mb4_slovenian_ci", 228},
    {"utf8mb4_polish_ci", 229},
    {"utf8mb4_estonian_ci", 230},
    {"utf8mb4_spanish_ci", 231},
    {"utf8mb4_swedish_ci", 232},
    {"utf8mb4_turkish_ci", 233},
    {"utf8mb4_czech_ci", 234},
    {"utf8mb4_danish_ci", 235},
    {"utf8mb4_lithuanian_ci", 236},
    {"utf8mb4_slovak_ci", 237},
    {"utf8mb4_spanish2_ci", 238},
    {"utf8mb4_roman_ci", 239},
    {"utf8mb4_persian_ci", 240},
    {"utf8mb4_esperanto_ci", 241},
    {"utf8mb4_hungarian_ci", 242},
    {"utf8mb4_sinhala_ci", 243},
    {"utf8mb4_german2_ci", 244},
    {"utf8mb4_croatian_ci", 245},
    {"utf8mb4_unicode_520_ci", 246},
    {"utf8mb4_vietnamese_ci", 247},
    {"gb18030_chinese_ci", 248},
    {"gb18030_bin", 249},
    {"gb18030_unicode_520_ci", 250},
    {"utf8mb4_0900_ai_ci", 255},
};


const char* MysqlDefaultCollation = "utf8mb4_general_ci";

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
