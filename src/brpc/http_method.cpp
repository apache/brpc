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


#include "butil/macros.h"
#include "butil/logging.h"
#include <pthread.h>
#include <algorithm>
#include "brpc/http_method.h"

namespace brpc {

struct HttpMethodPair {
    HttpMethod method;
    const char *str;
};

static HttpMethodPair g_method_pairs[] = {
    { HTTP_METHOD_DELETE      ,   "DELETE"      },
    { HTTP_METHOD_GET         ,   "GET"         },
    { HTTP_METHOD_HEAD        ,   "HEAD"        },
    { HTTP_METHOD_POST        ,   "POST"        },
    { HTTP_METHOD_PUT         ,   "PUT"         },
    { HTTP_METHOD_CONNECT     ,   "CONNECT"     },
    { HTTP_METHOD_OPTIONS     ,   "OPTIONS"     },
    { HTTP_METHOD_TRACE       ,   "TRACE"       },
    { HTTP_METHOD_COPY        ,   "COPY"        },
    { HTTP_METHOD_LOCK        ,   "LOCK"        },
    { HTTP_METHOD_MKCOL       ,   "MKCOL"       },
    { HTTP_METHOD_MOVE        ,   "MOVE"        },
    { HTTP_METHOD_PROPFIND    ,   "PROPFIND"    },
    { HTTP_METHOD_PROPPATCH   ,   "PROPPATCH"   },
    { HTTP_METHOD_SEARCH      ,   "SEARCH"      },
    { HTTP_METHOD_UNLOCK      ,   "UNLOCK"      },
    { HTTP_METHOD_REPORT      ,   "REPORT"      },
    { HTTP_METHOD_MKACTIVITY  ,   "MKACTIVITY"  },
    { HTTP_METHOD_CHECKOUT    ,   "CHECKOUT"    },
    { HTTP_METHOD_MERGE       ,   "MERGE"       },
    { HTTP_METHOD_MSEARCH     ,   "M-SEARCH"    },
    { HTTP_METHOD_NOTIFY      ,   "NOTIFY"      },
    { HTTP_METHOD_SUBSCRIBE   ,   "SUBSCRIBE"   },
    { HTTP_METHOD_UNSUBSCRIBE ,   "UNSUBSCRIBE" },
    { HTTP_METHOD_PATCH       ,   "PATCH"       },
    { HTTP_METHOD_PURGE       ,   "PURGE"       },
    { HTTP_METHOD_MKCALENDAR  ,   "MKCALENDAR"  },
};

static const char* g_method2str_map[64] = { NULL };
static pthread_once_t g_init_maps_once = PTHREAD_ONCE_INIT;
static uint8_t g_first_char_index[26] = { 0 };

struct LessThanByName {
    bool operator()(const HttpMethodPair& p1, const HttpMethodPair& p2) const {
        return strcasecmp(p1.str, p2.str) < 0;
    }
};

static void BuildHttpMethodMaps() {
    for (size_t i = 0; i < ARRAY_SIZE(g_method_pairs); ++i) {
        const int method = (int)g_method_pairs[i].method;
        RELEASE_ASSERT(method >= 0 &&
                       method <= (int)ARRAY_SIZE(g_method2str_map));
        g_method2str_map[method] = g_method_pairs[i].str;
     }
    std::sort(g_method_pairs, g_method_pairs + ARRAY_SIZE(g_method_pairs),
              LessThanByName());
    char last_fc = '\0';
    for (size_t i = 0; i < ARRAY_SIZE(g_method_pairs); ++i) {
        char fc = g_method_pairs[i].str[0];
        RELEASE_ASSERT_VERBOSE(fc >= 'A' && fc <= 'Z',
                               "Invalid method_name=%s",
                               g_method_pairs[i].str);
        if (fc != last_fc) {
            last_fc = fc;
            g_first_char_index[fc - 'A'] = (uint8_t)(i + 1);
        }
    }
}

const char *HttpMethod2Str(HttpMethod method) {
    pthread_once(&g_init_maps_once, BuildHttpMethodMaps);
    if ((int)method < 0 ||
        (int)method >= (int)ARRAY_SIZE(g_method2str_map)) {
        return "UNKNOWN";
    }
    const char* s = g_method2str_map[method];
    return s ? s : "UNKNOWN";
}

bool Str2HttpMethod(const char* method_str, HttpMethod* method) {
    const char fc = ::toupper(*method_str);
    if (fc == 'G') {
        if (strcasecmp(method_str + 1, /*G*/"ET") == 0) {
            *method = HTTP_METHOD_GET;
            return true;
        }
    } else if (fc == 'P') {
        if (strcasecmp(method_str + 1, /*P*/"OST") == 0) {
            *method = HTTP_METHOD_POST;
            return true;
        } else if (strcasecmp(method_str + 1, /*P*/"UT") == 0) {
            *method = HTTP_METHOD_PUT;
            return true;
        }
    }
    pthread_once(&g_init_maps_once, BuildHttpMethodMaps);
    if (fc < 'A' || fc > 'Z') {
        return false;
    }
    size_t index = g_first_char_index[fc - 'A'];
    if (index == 0) {
        return false;
    }
    --index;
    for (; index < ARRAY_SIZE(g_method_pairs); ++index) {
        const HttpMethodPair& p = g_method_pairs[index];
        if (strcasecmp(method_str, p.str) == 0) {
            *method = p.method;
            return true;
        }
        if (p.str[0] != fc) {
            return false;
        }
    }
    return false;
}

} // namespace brpc
