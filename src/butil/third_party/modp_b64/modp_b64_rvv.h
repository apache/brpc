/* ... Apache 2.0 license header ... */
#ifndef BUTIL_MODP_B64_RVV_H
#define BUTIL_MODP_B64_RVV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__riscv) && defined(__riscv_vector)
#include <riscv_vector.h>

size_t rvv_modp_b64_encode(char* dest, const char* str, size_t len);
size_t rvv_modp_b64_decode(char* dest, const char* src, size_t len);
#endif /* __riscv_vector */

#ifdef __cplusplus
}
#endif

#endif /* BUTIL_MODP_B64_RVV_H */
