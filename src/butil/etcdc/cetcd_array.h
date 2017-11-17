#ifndef CETCD_ARRAY_H
#define CETCD_ARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

typedef struct cetcd_array_t {
    void **elem;
    size_t count;
    size_t cap;
} cetcd_array;

size_t cetcd_array_size(cetcd_array *ca);
size_t cetcd_array_cap(cetcd_array *ca); 

cetcd_array *cetcd_array_create(size_t cap);
void         cetcd_array_release(cetcd_array *ca);

int cetcd_array_init(cetcd_array *ca, size_t cap);
int cetcd_array_destroy(cetcd_array *ca);
int cetcd_array_append(cetcd_array *ca, void *p);

void *cetcd_array_get(cetcd_array *ca, size_t index);
int   cetcd_array_set(cetcd_array *ca, size_t index, void *p);
void *cetcd_array_top(cetcd_array *ca);
void *cetcd_array_pop(cetcd_array *ca);

cetcd_array *cetcd_array_shuffle(cetcd_array *cards);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
