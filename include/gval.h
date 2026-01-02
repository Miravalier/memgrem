#ifndef _MEMGREM_GVAL_H
#define _MEMGREM_GVAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef enum search_op_e {
    SEARCH_NOOP,
    SEARCH_EQUAL,
    SEARCH_LESS,
    SEARCH_GREATER,
    SEARCH_APPROX,
} search_op_e;


typedef enum gval_type {
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_UINT64,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_FLOAT32,
    TYPE_FLOAT64,
    TYPE_BYTES_16,
} gval_type_e;


typedef union gval {
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int64;
    float float32;
    double float64;
    uint8_t b16[16];
} gval_u;

size_t gval_size(gval_type_e type);
void gval_move(gval_type_e type, void *dst, const void *src);
bool gval_compare(gval_type_e type, search_op_e op, const void *a, const void *b);

#endif
