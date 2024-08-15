#pragma once

#include <brpc/controller.h>

namespace brpc {

bool HasJemalloc();

// env need MALLOC_CONF="prof:true" before process start
bool HasEnableJemallocProfile();

void JeControlProfile(Controller* cntl);

}

