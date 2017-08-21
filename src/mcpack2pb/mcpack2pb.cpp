// mcpack2pb - Make protobuf be front-end of mcpack/compack
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 19 17:17:36 CST 2015

#include <gflags/gflags.h>
#include "mcpack2pb/mcpack2pb.h"

DEFINE_bool(mcpack2pb_absent_field_is_error, false, "Parsing fails if the "
            "field in compack/mcpack does not exist in protobuf");

namespace mcpack2pb {

static pthread_once_t s_init_handler_map_once = PTHREAD_ONCE_INIT;
static base::FlatMap<std::string, MessageHandler>* s_handler_map = NULL;
static void init_handler_map() {
    s_handler_map = new base::FlatMap<std::string, MessageHandler>;
    if (s_handler_map->init(64, 50) != 0) {
        LOG(ERROR) << "Fail to init s_handler_map";
        exit(1);
    }
}
void register_message_handler_or_die(const std::string& full_name,
                                     const MessageHandler& handler) {
    pthread_once(&s_init_handler_map_once, init_handler_map);
    if (s_handler_map->seek(full_name) != NULL) {
        LOG(ERROR) << full_name << " was registered before!";
        exit(1);
    } else {
        (*s_handler_map)[full_name] = handler;
    }
}

MessageHandler find_message_handler(const std::string& full_name) {
    pthread_once(&s_init_handler_map_once, init_handler_map);
    MessageHandler* handler = s_handler_map->seek(full_name);
    if (handler != NULL) {
        return *handler;
    }
    MessageHandler null_handler = { NULL, NULL, NULL, NULL };
    return null_handler;
}

} // namespace mcpack2pb
