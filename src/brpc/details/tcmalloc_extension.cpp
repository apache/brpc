#include "butil/compiler_specific.h"
#include "brpc/details/tcmalloc_extension.h"

MallocExtension* BAIDU_WEAK MallocExtension::instance() {
    return NULL;
}
