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

#ifndef BRPC_MYSQL_COMMON_H
#define BRPC_MYSQL_COMMON_H

#include <sstream>

namespace brpc {
// Msql Collation
enum MysqlCollation : uint16_t {
    MYSQL_big5_chinese_ci = 1,
    MYSQL_latin2_czech_cs = 2,
    MYSQL_dec8_swedish_ci = 3,
    MYSQL_cp850_general_ci = 4,
    MYSQL_latin1_german1_ci = 5,
    MYSQL_hp8_english_ci = 6,
    MYSQL_koi8r_general_ci = 7,
    MYSQL_latin1_swedish_ci = 8,
    MYSQL_latin2_general_ci = 9,
    MYSQL_swe7_swedish_ci = 10,
    MYSQL_ascii_general_ci = 11,
    MYSQL_ujis_japanese_ci = 12,
    MYSQL_sjis_japanese_ci = 13,
    MYSQL_cp1251_bulgarian_ci = 14,
    MYSQL_latin1_danish_ci = 15,
    MYSQL_hebrew_general_ci = 16,
    MYSQL_tis620_thai_ci = 18,
    MYSQL_euckr_korean_ci = 19,
    MYSQL_latin7_estonian_cs = 20,
    MYSQL_latin2_hungarian_ci = 21,
    MYSQL_koi8u_general_ci = 22,
    MYSQL_cp1251_ukrainian_ci = 23,
    MYSQL_gb2312_chinese_ci = 24,
    MYSQL_greek_general_ci = 25,
    MYSQL_cp1250_general_ci = 26,
    MYSQL_latin2_croatian_ci = 27,
    MYSQL_gbk_chinese_ci = 28,
    MYSQL_cp1257_lithuanian_ci = 29,
    MYSQL_latin5_turkish_ci = 30,
    MYSQL_latin1_german2_ci = 31,
    MYSQL_armscii8_general_ci = 32,
    MYSQL_utf8_general_ci = 33,
    MYSQL_cp1250_czech_cs = 34,
    MYSQL_ucs2_general_ci = 35,
    MYSQL_cp866_general_ci = 36,
    MYSQL_keybcs2_general_ci = 37,
    MYSQL_macce_general_ci = 38,
    MYSQL_macroman_general_ci = 39,
    MYSQL_cp852_general_ci = 40,
    MYSQL_latin7_general_ci = 41,
    MYSQL_latin7_general_cs = 42,
    MYSQL_macce_bin = 43,
    MYSQL_cp1250_croatian_ci = 44,
    MYSQL_utf8mb4_general_ci = 45,
    MYSQL_utf8mb4_bin = 46,
    MYSQL_latin1_bin = 47,
    MYSQL_latin1_general_ci = 48,
    MYSQL_latin1_general_cs = 49,
    MYSQL_cp1251_bin = 50,
    MYSQL_cp1251_general_ci = 51,
    MYSQL_cp1251_general_cs = 52,
    MYSQL_macroman_bin = 53,
    MYSQL_utf16_general_ci = 54,
    MYSQL_utf16_bin = 55,
    MYSQL_utf16le_general_ci = 56,
    MYSQL_cp1256_general_ci = 57,
    MYSQL_cp1257_bin = 58,
    MYSQL_cp1257_general_ci = 59,
    MYSQL_utf32_general_ci = 60,
    MYSQL_utf32_bin = 61,
    MYSQL_utf16le_bin = 62,
    MYSQL_binary = 63,
    MYSQL_armscii8_bin = 64,
    MYSQL_ascii_bin = 65,
    MYSQL_cp1250_bin = 66,
    MYSQL_cp1256_bin = 67,
    MYSQL_cp866_bin = 68,
    MYSQL_dec8_bin = 69,
    MYSQL_greek_bin = 70,
    MYSQL_hebrew_bin = 71,
    MYSQL_hp8_bin = 72,
    MYSQL_keybcs2_bin = 73,
    MYSQL_koi8r_bin = 74,
    MYSQL_koi8u_bin = 75,
    MYSQL_latin2_bin = 77,
    MYSQL_latin5_bin = 78,
    MYSQL_latin7_bin = 79,
    MYSQL_cp850_bin = 80,
    MYSQL_cp852_bin = 81,
    MYSQL_swe7_bin = 82,
    MYSQL_utf8_bin = 83,
    MYSQL_big5_bin = 84,
    MYSQL_euckr_bin = 85,
    MYSQL_gb2312_bin = 86,
    MYSQL_gbk_bin = 87,
    MYSQL_sjis_bin = 88,
    MYSQL_tis620_bin = 89,
    MYSQL_ucs2_bin = 90,
    MYSQL_ujis_bin = 91,
    MYSQL_geostd8_general_ci = 92,
    MYSQL_geostd8_bin = 93,
    MYSQL_latin1_spanish_ci = 94,
    MYSQL_cp932_japanese_ci = 95,
    MYSQL_cp932_bin = 96,
    MYSQL_eucjpms_japanese_ci = 97,
    MYSQL_eucjpms_bin = 98,
    MYSQL_cp1250_polish_ci = 99,
    MYSQL_utf16_unicode_ci = 101,
    MYSQL_utf16_icelandic_ci = 102,
    MYSQL_utf16_latvian_ci = 103,
    MYSQL_utf16_romanian_ci = 104,
    MYSQL_utf16_slovenian_ci = 105,
    MYSQL_utf16_polish_ci = 106,
    MYSQL_utf16_estonian_ci = 107,
    MYSQL_utf16_spanish_ci = 108,
    MYSQL_utf16_swedish_ci = 109,
    MYSQL_utf16_turkish_ci = 110,
    MYSQL_utf16_czech_ci = 111,
    MYSQL_utf16_danish_ci = 112,
    MYSQL_utf16_lithuanian_ci = 113,
    MYSQL_utf16_slovak_ci = 114,
    MYSQL_utf16_spanish2_ci = 115,
    MYSQL_utf16_roman_ci = 116,
    MYSQL_utf16_persian_ci = 117,
    MYSQL_utf16_esperanto_ci = 118,
    MYSQL_utf16_hungarian_ci = 119,
    MYSQL_utf16_sinhala_ci = 120,
    MYSQL_utf16_german2_ci = 121,
    MYSQL_utf16_croatian_ci = 122,
    MYSQL_utf16_unicode_520_ci = 123,
    MYSQL_utf16_vietnamese_ci = 124,
    MYSQL_ucs2_unicode_ci = 128,
    MYSQL_ucs2_icelandic_ci = 129,
    MYSQL_ucs2_latvian_ci = 130,
    MYSQL_ucs2_romanian_ci = 131,
    MYSQL_ucs2_slovenian_ci = 132,
    MYSQL_ucs2_polish_ci = 133,
    MYSQL_ucs2_estonian_ci = 134,
    MYSQL_ucs2_spanish_ci = 135,
    MYSQL_ucs2_swedish_ci = 136,
    MYSQL_ucs2_turkish_ci = 137,
    MYSQL_ucs2_czech_ci = 138,
    MYSQL_ucs2_danish_ci = 139,
    MYSQL_ucs2_lithuanian_ci = 140,
    MYSQL_ucs2_slovak_ci = 141,
    MYSQL_ucs2_spanish2_ci = 142,
    MYSQL_ucs2_roman_ci = 143,
    MYSQL_ucs2_persian_ci = 144,
    MYSQL_ucs2_esperanto_ci = 145,
    MYSQL_ucs2_hungarian_ci = 146,
    MYSQL_ucs2_sinhala_ci = 147,
    MYSQL_ucs2_german2_ci = 148,
    MYSQL_ucs2_croatian_ci = 149,
    MYSQL_ucs2_unicode_520_ci = 150,
    MYSQL_ucs2_vietnamese_ci = 151,
    MYSQL_ucs2_general_mysql500_ci = 159,
    MYSQL_utf32_unicode_ci = 160,
    MYSQL_utf32_icelandic_ci = 161,
    MYSQL_utf32_latvian_ci = 162,
    MYSQL_utf32_romanian_ci = 163,
    MYSQL_utf32_slovenian_ci = 164,
    MYSQL_utf32_polish_ci = 165,
    MYSQL_utf32_estonian_ci = 166,
    MYSQL_utf32_spanish_ci = 167,
    MYSQL_utf32_swedish_ci = 168,
    MYSQL_utf32_turkish_ci = 169,
    MYSQL_utf32_czech_ci = 170,
    MYSQL_utf32_danish_ci = 171,
    MYSQL_utf32_lithuanian_ci = 172,
    MYSQL_utf32_slovak_ci = 173,
    MYSQL_utf32_spanish2_ci = 174,
    MYSQL_utf32_roman_ci = 175,
    MYSQL_utf32_persian_ci = 176,
    MYSQL_utf32_esperanto_ci = 177,
    MYSQL_utf32_hungarian_ci = 178,
    MYSQL_utf32_sinhala_ci = 179,
    MYSQL_utf32_german2_ci = 180,
    MYSQL_utf32_croatian_ci = 181,
    MYSQL_utf32_unicode_520_ci = 182,
    MYSQL_utf32_vietnamese_ci = 183,
    MYSQL_utf8_unicode_ci = 192,
    MYSQL_utf8_icelandic_ci = 193,
    MYSQL_utf8_latvian_ci = 194,
    MYSQL_utf8_romanian_ci = 195,
    MYSQL_utf8_slovenian_ci = 196,
    MYSQL_utf8_polish_ci = 197,
    MYSQL_utf8_estonian_ci = 198,
    MYSQL_utf8_spanish_ci = 199,
    MYSQL_utf8_swedish_ci = 200,
    MYSQL_utf8_turkish_ci = 201,
    MYSQL_utf8_czech_ci = 202,
    MYSQL_utf8_danish_ci = 203,
    MYSQL_utf8_lithuanian_ci = 204,
    MYSQL_utf8_slovak_ci = 205,
    MYSQL_utf8_spanish2_ci = 206,
    MYSQL_utf8_roman_ci = 207,
    MYSQL_utf8_persian_ci = 208,
    MYSQL_utf8_esperanto_ci = 209,
    MYSQL_utf8_hungarian_ci = 210,
    MYSQL_utf8_sinhala_ci = 211,
    MYSQL_utf8_german2_ci = 212,
    MYSQL_utf8_croatian_ci = 213,
    MYSQL_utf8_unicode_520_ci = 214,
    MYSQL_utf8_vietnamese_ci = 215,
    MYSQL_utf8_general_mysql500_ci = 223,
    MYSQL_utf8mb4_unicode_ci = 224,
    MYSQL_utf8mb4_icelandic_ci = 225,
    MYSQL_utf8mb4_latvian_ci = 226,
    MYSQL_utf8mb4_romanian_ci = 227,
    MYSQL_utf8mb4_slovenian_ci = 228,
    MYSQL_utf8mb4_polish_ci = 229,
    MYSQL_utf8mb4_estonian_ci = 230,
    MYSQL_utf8mb4_spanish_ci = 231,
    MYSQL_utf8mb4_swedish_ci = 232,
    MYSQL_utf8mb4_turkish_ci = 233,
    MYSQL_utf8mb4_czech_ci = 234,
    MYSQL_utf8mb4_danish_ci = 235,
    MYSQL_utf8mb4_lithuanian_ci = 236,
    MYSQL_utf8mb4_slovak_ci = 237,
    MYSQL_utf8mb4_spanish2_ci = 238,
    MYSQL_utf8mb4_roman_ci = 239,
    MYSQL_utf8mb4_persian_ci = 240,
    MYSQL_utf8mb4_esperanto_ci = 241,
    MYSQL_utf8mb4_hungarian_ci = 242,
    MYSQL_utf8mb4_sinhala_ci = 243,
    MYSQL_utf8mb4_german2_ci = 244,
    MYSQL_utf8mb4_croatian_ci = 245,
    MYSQL_utf8mb4_unicode_520_ci = 246,
    MYSQL_utf8mb4_vietnamese_ci = 247,
};

const int mysql_header_size = 4;
const uint32_t mysql_max_package_size = 0xFFFFFF;

inline std::string pack_encode_length(const uint64_t value) {
    std::stringstream ss;
    if (value <= 250) {
        ss.put((char)value);
    } else if (value <= 0xffff) {
        ss.put((char)0xfc).put((char)value).put((char)(value >> 8));
    } else if (value <= 0xffffff) {
        ss.put((char)0xfd).put((char)value).put((char)(value >> 8)).put((char)(value >> 16));
    } else {
        ss.put((char)0xfd)
            .put((char)value)
            .put((char)(value >> 8))
            .put((char)(value >> 16))
            .put((char)(value >> 24))
            .put((char)(value >> 32))
            .put((char)(value >> 40))
            .put((char)(value >> 48))
            .put((char)(value >> 56));
    }
    return ss.str();
}

// little endian order to host order
inline uint16_t mysql_uint2korr(const uint8_t* A) {
    return (uint16_t)(((uint16_t)(A[0])) + ((uint16_t)(A[1]) << 8));
}
inline uint32_t mysql_uint3korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16));
}
inline uint32_t mysql_uint4korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16) +
                      (((uint32_t)(A[3])) << 24));
}
inline uint64_t mysql_uint8korr(const uint8_t* A) {
    return (uint64_t)(((uint64_t)(A[0])) + (((uint64_t)(A[1])) << 8) + (((uint64_t)(A[2])) << 16) +
                      (((uint64_t)(A[3])) << 24) + (((uint64_t)(A[4])) << 32) +
                      (((uint64_t)(A[5])) << 40) + (((uint64_t)(A[6])) << 48) +
                      (((uint64_t)(A[7])) << 56));
}
}  // namespace brpc
#endif
