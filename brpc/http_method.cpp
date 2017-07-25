// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/26 15:01:15

#include "brpc/http_method.h"

#include <stdlib.h>                     // abort()

#include "base/macros.h"


namespace brpc {

struct MethodMap {
    HttpMethod method;
    const char *str;
} method_map[] = {
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
    { HTTP_METHOD_MSEARCH     ,   "M-SEARCH"    },  // M-SEARCH
    { HTTP_METHOD_NOTIFY      ,   "NOTIFY"      },
    { HTTP_METHOD_SUBSCRIBE   ,   "SUBSCRIBE"   },
    { HTTP_METHOD_UNSUBSCRIBE ,   "UNSUBSCRIBE" },
    { HTTP_METHOD_PATCH       ,   "PATCH"       },
    { HTTP_METHOD_PURGE       ,   "PURGE"       },
    { HTTP_METHOD_MKCALENDAR  ,   "MKCALENDAR"  },
};

static const char *http_method_str[64] = { NULL };

BAIDU_GLOBAL_INIT() {
    for (size_t i = 0; i < ARRAY_SIZE(method_map); ++i) {
        if (method_map[i].method < 0 ||
            method_map[i].method > (int)ARRAY_SIZE(http_method_str)) {
            abort();
        }
        http_method_str[method_map[i].method] = method_map[i].str;
    }
}

const char *GetMethodStr(HttpMethod http_method) {
    if (http_method < 0 ||
        http_method >= (int)ARRAY_SIZE(http_method_str) ||
        http_method_str[http_method] == NULL) {
        return "UNKNOWN";
    }
    return http_method_str[http_method];
}

} // namespace brpc

