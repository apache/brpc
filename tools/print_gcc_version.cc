#include <stdio.h>
int main() {
#if defined(__clang__)
    const int major_v = __GNUC__;
    int minor_v = __GNUC_MINOR__;
    if (major_v == 4 && minor_v <= 8) {
        // Make version of clang >= 4.8 so that it's not rejected by config_brpc.sh
        minor_v = 8;
    }
    printf("%d\n", (major_v * 10000 + minor_v * 100));
#elif defined(__GNUC__)
    printf("%d\n", (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__));
#else
    printf("0\n");
#endif
    return 0;
}
