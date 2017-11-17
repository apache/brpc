#include <stdlib.h>
#include <time.h>

#include "cetcd_array.h"

cetcd_array *cetcd_array_create(size_t cap) {
    cetcd_array *ca;

    ca = calloc(1, sizeof(cetcd_array));
    cetcd_array_init(ca, cap);
    return ca;
}
void cetcd_array_release(cetcd_array *ca) {
    if (ca) {
        cetcd_array_destroy(ca);
        free(ca);
    }
}
int cetcd_array_init(cetcd_array *ca, size_t cap) {
    ca->count = 0;
    ca->cap = cap;
    ca->elem = NULL;

    ca->elem = malloc(sizeof(void *) * ca->cap);
    if (ca->elem == NULL) {
        return -1;
    }
    return 0;
}

int cetcd_array_set(cetcd_array *ca, size_t index, void *p) {
    if (index > ca->count) {
        return -1;
    }
    ca->elem[index] = p;
    return 0;
}

int cetcd_array_append(cetcd_array *ca, void *p) {
    size_t left;

    left = ca->cap - ca->count;
    /* The array is full, resize it by power 2*/
    if (left == 0) {
        ca->cap = ca->cap * 2;
        ca->elem = realloc(ca->elem, sizeof(void *) * ca->cap);
        if (ca->elem == NULL) {
            return -1;
        }
    }

    ca->elem[ca->count] = p;
    ca->count ++;
    return 0;
}

void *cetcd_array_top(cetcd_array *ca) {
    return cetcd_array_get(ca, cetcd_array_size(ca) - 1);
}
void *cetcd_array_pop(cetcd_array *ca) {
    void *e = NULL;
    if (cetcd_array_size(ca) > 0) {
        e = cetcd_array_get(ca, cetcd_array_size(ca) - 1);
        -- ca->count;
    }
    return e;
}

int cetcd_array_destroy(cetcd_array *ca) {
    if (ca->elem != NULL && ca->cap != 0) {
        free(ca->elem);
        ca->elem = NULL;
    }
    ca->count = 0;
    ca->cap   = 0;
    return 0;
}

void *cetcd_array_get(cetcd_array *ca, size_t index) {
    if (index > ca->count) {
        return NULL;
    }
    return ca->elem[index];
}

size_t cetcd_array_size(cetcd_array *ca) {
    return ca->count;
}

size_t cetcd_array_cap(cetcd_array *ca) {
    return ca->cap;
}
cetcd_array *cetcd_array_shuffle(cetcd_array *cards) {
    int i, j, count;
    void *src, *dst;

    srand(time(0));
    count = cetcd_array_size(cards);
    if (count <= 1) {
        return cards;
    }
    for (i = count-1; i > 0; --i) {
        j = rand() % (i+1);
        if (i != j) {
            src = cetcd_array_get(cards, i);
            dst = cetcd_array_get(cards, j);
            cetcd_array_set(cards, i, dst);
            cetcd_array_set(cards, j, src);
        }
    }

    return cards;
}
