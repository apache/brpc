// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon Sep 15 10:51:15 CST 2014

#ifndef PUBLIC_BTHREAD_BTHREAD_COMLOG_INITIALIZER_H
#define PUBLIC_BTHREAD_BTHREAD_COMLOG_INITIALIZER_H

#include <com_log.h>                       // com_openlog_r, com_closelog_r
#include <base/macros.h>

namespace bthread {

class ComlogInitializer {
public:
    ComlogInitializer() {
        if (com_logstatus() != LOG_NOT_DEFINED) {
            com_openlog_r();
        }
    }
    ~ComlogInitializer() {
        if (com_logstatus() != LOG_NOT_DEFINED) {
            com_closelog_r();
        }
    }
    
private:
    DISALLOW_COPY_AND_ASSIGN(ComlogInitializer);
};

}

#endif // PUBLIC_BTHREAD_BTHREAD_COMLOG_INITIALIZER_H
