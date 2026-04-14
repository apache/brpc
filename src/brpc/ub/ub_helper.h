#ifndef BRPC_UB_HELPER_H
#define BRPC_UB_HELPER_H

#if BRPC_WITH_UBRING

#include <string>
#include <functional>
#include "bthread/types.h"


namespace brpc {
    namespace ub {

        void GlobalRelease();

        void GlobalUBInitializeOrDie();

        bool InitPollingModeWithTag(bthread_tag_t tag,
                                    std::function<void(void)> callback = nullptr,
                                    std::function<void(void)> init_fn = nullptr,
                                    std::function<void(void)> release_fn = nullptr);

        // If the UB environment is available
        bool IsUBAvailable();

        // Disable UB in the remaining lifetime of the process
        void GlobalDisableUb();

        // If the given protocol supported by UB
        bool SupportedByUB(std::string protocol);

    }  // namespace ub
}  // namespace brpc
#else
namespace brpc {
    namespace ub {

        void GlobalRelease();

        // Initialize UB environment
        // Exit if failed
        void GlobalUBInitializeOrDie();

    }  // namespace ub
}  // namespace brpc
#endif  // if BRPC_WITH_UBRING

#endif //BRPC_UB_HELPER_H
