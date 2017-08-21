#include <stdio.h>
int main() {
#if defined(__GNUC__)
    printf("%d\n", (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__));
#else
    printf("0\n");
#endif
    return 0;
}
