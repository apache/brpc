#include <yajl/yajl_parse.h>
#include <string.h>
#include <stdlib.h>
#include "cetcd.h"

typedef struct yajl_parser_context_t {
    void *userdata;
    cetcd_array keystack;   
    cetcd_array nodestack;
} yajl_parser_context;
#define EQ(s, d) ((strncmp((s), (d), sizeof((d)) - 1) == 0) && (sdslen(s) == (sizeof((d))-1)))
#define UNUSED(v) (void)(v)
static int cetcd_parse_action(cetcd_string act) {
    if (EQ(act, "set")) {
        return ETCD_SET;
    }
    if (EQ(act, "update")) {
        return ETCD_UPDATE;
    }
    if (EQ(act, "get")) {
        return ETCD_GET;
    }
    if (EQ(act, "delete")) {
        return ETCD_DELETE;
    }
    if (EQ(act, "create")) {
        return ETCD_CREATE;
    }
    if (EQ(act, "expire")) {
        return ETCD_EXPIRE;
    }
    if (EQ(act, "compareAndSwap")) {
        return ETCD_CAS;
    }
    if (EQ(act, "compareAndDelete")) {
        return ETCD_CAD;
    }
    return -1;
}
static int yajl_parse_bool_cb(void *ctx, int val) {
    yajl_parser_context *c = ctx;
    cetcd_response_node *node;
    cetcd_string key;

    key = cetcd_array_pop(&c->keystack);
    if (EQ(key, "dir")) {
        node = cetcd_array_top(&c->nodestack);
        node->dir = val;
    }
    sdsfree(key);
    return 1;
}
static int yajl_parse_integer_cb(void *ctx, long long val) {
    yajl_parser_context *c = ctx;
    cetcd_response_node *node;
    cetcd_string key;

    key = cetcd_array_pop(&c->keystack);
    node = cetcd_array_top(&c->nodestack);
    if (EQ(key, "ttl")) {
        node->ttl = (int64_t)val;
    }
    else if (EQ(key, "modifiedindex")) {
        node->modified_index = (uint64_t) val;
    }
    else if (EQ(key, "createdindex")) {
        node->created_index = (uint64_t) val;
    }
    else if (EQ(key, "expiration")) {
        node->expiration = (uint64_t) val;
    }
    sdsfree(key);
    
    return 1;
}
static int yajl_parse_string_cb(void *ctx, unsigned const char *val, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_response_node *node;
    cetcd_response *resp;
    cetcd_string key, value;

    key = cetcd_array_pop(&c->keystack);
    if (EQ(key, "key")) {
        node = cetcd_array_top(&c->nodestack);
        node->key = sdsnewlen(val, len);
    }
    else if (EQ(key, "value")) {
        node = cetcd_array_top(&c->nodestack);
        node->value = sdsnewlen(val, len);
    }
    else if (EQ(key, "action")) {
        resp = c->userdata;
        value = sdsnewlen(val, len);
        resp->action = cetcd_parse_action(value);
        sdsfree(value);
    }
    sdsfree(key);
    return 1;
}
static int yajl_parse_start_map_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_string key;
    cetcd_response_node *node, *child;

    /*this is key of nodes*/
    if (cetcd_array_size(&c->keystack) > 0) {
        key = cetcd_array_top(&c->keystack);
        node = cetcd_array_top(&c->nodestack);
        if (EQ(key, "nodes")) {
            child = calloc(1, sizeof(cetcd_response_node));
            cetcd_array_append(node->nodes, child);
            cetcd_array_append(&c->nodestack, child);
            cetcd_array_append(&c->keystack, sdsnew("noname"));
        }
        return 1;
    }
    /*this is a cetcd_response*/
    if (c->userdata == NULL) {
        c->userdata = calloc(1, sizeof(cetcd_response));
    }

    return 1;
}
static int yajl_parse_map_key_cb(void *ctx, const unsigned char *key, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_response *resp = (cetcd_response *)c->userdata;

    cetcd_string name = sdsnewlen(key, len);
    sdstolower(name);
    cetcd_array_append(&c->keystack, name);

    if (EQ(name, "node")) {
        resp->node = calloc(1, sizeof(cetcd_response_node));
        cetcd_array_append(&c->nodestack, resp->node);
    }
    else if (EQ(name, "prevnode")) {
        resp->prev_node = calloc(1, sizeof(cetcd_response_node));
        cetcd_array_append(&c->nodestack, resp->prev_node);
    }
    return 1;
}
static int yajl_parse_end_map_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_string key = cetcd_array_pop(&c->keystack);
    if (key) {
        sdsfree(key);
    }
    cetcd_array_pop(&c->nodestack);
    return 1;
}
static int yajl_parse_start_array_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_response_node *node;
    cetcd_string key;

    key = cetcd_array_top(&c->keystack);
    node = (cetcd_response_node *)cetcd_array_top(&c->nodestack);
    if (EQ(key, "nodes")) {
        if (node) {
            node->nodes = cetcd_array_create(10);
        }
    }
    return 1;
}
static int yajl_parse_end_array_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_string key;

    key = (cetcd_string)cetcd_array_top(&c->keystack);
    if (EQ(key, "nodes")) {
        sdsfree(cetcd_array_pop(&c->keystack));
    }
    return 1;
}

static int yajl_parse_null_ignore_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    sdsfree(cetcd_array_pop(&c->keystack));
    /*just ignore*/
    return 1;
}
static int yajl_parse_double_ignore_cb(void *ctx, double val) {
    UNUSED(val);
    return yajl_parse_null_ignore_cb(ctx);
}
static int yajl_parse_bool_ignore_cb(void *ctx, int val) {
    UNUSED(val);
    return yajl_parse_null_ignore_cb(ctx);
}
static yajl_callbacks callbacks = {
    yajl_parse_null_ignore_cb, //null
    yajl_parse_bool_cb, //boolean
    yajl_parse_integer_cb, //integer
    yajl_parse_double_ignore_cb, //double
    NULL, //number
    yajl_parse_string_cb, //string
    yajl_parse_start_map_cb, //start map
    yajl_parse_map_key_cb, //map key
    yajl_parse_end_map_cb, //end map
    yajl_parse_start_array_cb, //start array
    yajl_parse_end_array_cb //end array
};

/* Error message parse functions
 * Parsing error response is more simple than a normal response, 
 * we do not have to handle the nested objects , so we do not need
 * a nodestack.
 * */
static int yajl_err_parse_integer_cb(void *ctx, long long val) {
    yajl_parser_context *c = ctx;
    cetcd_error *err = c->userdata;
    cetcd_string key = cetcd_array_pop(&c->keystack);
    if (EQ(key, "errorcode")) {
        err->ecode = (int)val;
    }
    sdsfree(key);
    return 1;
}
static int yajl_err_parse_string_cb(void *ctx, unsigned const char *val, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_error *err = c->userdata;
    cetcd_string key = cetcd_array_pop(&c->keystack);
    if (EQ(key, "message")) {
        err->message = sdsnewlen(val, len);
    }
    else if (EQ(key, "cause")) {
        err->cause = sdsnewlen(val, len);
    }
    sdsfree(key);

    return 1;
}
static int yajl_err_parse_start_map_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    if (c->userdata == NULL) {
        c->userdata = calloc(1, sizeof(cetcd_error));
    }
    return 1;
}
static int yajl_err_parse_map_key_cb(void *ctx, unsigned const char *key, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_string name = sdsnewlen(key, len);
    sdstolower(name);
    cetcd_array_append(&c->keystack, name);
    return 1;
}
static int yajl_err_parse_end_map_cb(void *ctx) {
    UNUSED(ctx);
    return 1;
}

static yajl_callbacks error_callbacks = {
    yajl_parse_null_ignore_cb, //null
    yajl_parse_bool_ignore_cb, //boolean
    yajl_err_parse_integer_cb, //integer
    yajl_parse_double_ignore_cb, //double
    NULL, //number
    yajl_err_parse_string_cb, //string
    yajl_err_parse_start_map_cb, //start map
    yajl_err_parse_map_key_cb, //map key
    yajl_err_parse_end_map_cb, //end map
    NULL,                //start array
    yajl_parse_null_ignore_cb //end array
};

static int yajl_sync_parse_string_cb(void *ctx, unsigned const char *val, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_array *array = c->userdata;
    cetcd_string key = cetcd_array_top(&c->keystack);
    if ( key  && EQ(key, "clientURLs")) {
        cetcd_array_append(array, sdsnewlen(val, len));
    }
    return 1;
}
static int yajl_sync_parse_start_map_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    if (c->userdata == NULL) {
        c->userdata = cetcd_array_create(10);
    }
    return 1;
}
static int yajl_sync_parse_map_key_cb(void *ctx, const unsigned char *key, size_t len) {
    yajl_parser_context *c = ctx;
    cetcd_string name = sdsnewlen(key, len);
    if (EQ(name, "clientURLs")) {
        cetcd_array_append(&c->keystack, name);
    } else {
        sdsfree(name);
    }
    return 1;
}
static int yajl_sync_parse_end_map_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_string key = cetcd_array_pop(&c->keystack);
    if (key) {
        sdsfree(key);
    }
    return 1;
}
static int yajl_sync_parse_end_array_cb(void *ctx) {
    yajl_parser_context *c = ctx;
    cetcd_string key;
    key = cetcd_array_top(&c->keystack);
    if (key && EQ(key, "clientURLs")) {
        sdsfree(cetcd_array_pop(&c->keystack));
    }
    return 1;
}

static yajl_callbacks sync_callbacks = {
    NULL,  //null
    NULL,  //boolean
    NULL, //integer
    NULL, //double
    NULL, //number
    yajl_sync_parse_string_cb, //string
    yajl_sync_parse_start_map_cb, //start map
    yajl_sync_parse_map_key_cb, //map key
    yajl_sync_parse_end_map_cb, //end map
    NULL, //start array
    yajl_sync_parse_end_array_cb //end array
};
