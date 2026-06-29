#ifndef _MEMGREM_GVAL_H
#define _MEMGREM_GVAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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


static inline size_t gval_size(gval_type_e type)
{
    switch (type)
    {
        case TYPE_UINT8: return sizeof(uint8_t);
        case TYPE_UINT16: return sizeof(uint16_t);
        case TYPE_UINT32: return sizeof(uint32_t);
        case TYPE_UINT64: return sizeof(uint64_t);
        case TYPE_INT8: return sizeof(int8_t);
        case TYPE_INT16: return sizeof(int16_t);
        case TYPE_INT32: return sizeof(int32_t);
        case TYPE_INT64: return sizeof(int64_t);
        case TYPE_FLOAT32: return sizeof(float);
        case TYPE_FLOAT64: return sizeof(double);
        case TYPE_BYTES_16: return 16;
    }
    return 0;
}

static inline void gval_move(gval_type_e type, void *dst, const void *src)
{
    if (type == TYPE_FLOAT32) {
        *(float*)dst = *(float*)src;
    } else if (type == TYPE_FLOAT64) {
        *(double*)dst = *(double*)src;
    } else if (type == TYPE_UINT8) {
        *(uint8_t*)dst = *(uint8_t*)src;
    } else if (type == TYPE_UINT16) {
        *(uint16_t*)dst = *(uint16_t*)src;
    } else if (type == TYPE_UINT32) {
        *(uint32_t*)dst = *(uint32_t*)src;
    } else if (type == TYPE_UINT64) {
        *(uint64_t*)dst = *(uint64_t*)src;
    } else if (type == TYPE_INT8) {
        *(int8_t*)dst = *(int8_t*)src;
    } else if (type == TYPE_INT16) {
        *(int16_t*)dst = *(int16_t*)src;
    } else if (type == TYPE_INT32) {
        *(int32_t*)dst = *(int32_t*)src;
    } else if (type == TYPE_INT64) {
        *(int64_t*)dst = *(int64_t*)src;
    } else if (type == TYPE_BYTES_16) {
        memcpy(dst, src, 16);
    }
}

static inline bool gval_compare(gval_type_e type, search_op_e op, const void *a, const void *b)
{
    if (op == SEARCH_NOOP) {
        return true;
    }

    if (type == TYPE_INT8) {
        if (op == SEARCH_EQUAL) {
            return *(int8_t*)a == *(int8_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(int8_t*)a >= *(int8_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(int8_t*)a <= *(int8_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(int8_t*)a == *(int8_t*)b;
        }
    } else if (type == TYPE_INT16) {
        if (op == SEARCH_EQUAL) {
            return *(int16_t*)a == *(int16_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(int16_t*)a >= *(int16_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(int16_t*)a <= *(int16_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(int16_t*)a == *(int16_t*)b;
        }
    } else if (type == TYPE_INT32) {
        if (op == SEARCH_EQUAL) {
            return *(int32_t*)a == *(int32_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(int32_t*)a >= *(int32_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(int32_t*)a <= *(int32_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(int32_t*)a == *(int32_t*)b;
        }
    } else if (type == TYPE_INT64) {
        if (op == SEARCH_EQUAL) {
            return *(int64_t*)a == *(int64_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(int64_t*)a >= *(int64_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(int64_t*)a <= *(int64_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(int64_t*)a == *(int64_t*)b;
        }
    } else if (type == TYPE_UINT8) {
        if (op == SEARCH_EQUAL) {
            return *(uint8_t*)a == *(uint8_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(uint8_t*)a >= *(uint8_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(uint8_t*)a <= *(uint8_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(uint8_t*)a == *(uint8_t*)b;
        }
    } else if (type == TYPE_UINT16) {
        if (op == SEARCH_EQUAL) {
            return *(uint16_t*)a == *(uint16_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(uint16_t*)a >= *(uint16_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(uint16_t*)a <= *(uint16_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(uint16_t*)a == *(uint16_t*)b;
        }
    } else if (type == TYPE_UINT32) {
        if (op == SEARCH_EQUAL) {
            return *(uint32_t*)a == *(uint32_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(uint32_t*)a >= *(uint32_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(uint32_t*)a <= *(uint32_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(uint32_t*)a == *(uint32_t*)b;
        }
    } else if (type == TYPE_UINT64) {
        if (op == SEARCH_EQUAL) {
            return *(uint64_t*)a == *(uint64_t*)b;
        } else if (op == SEARCH_GREATER) {
            return *(uint64_t*)a >= *(uint64_t*)b;
        } else if (op == SEARCH_LESS) {
            return *(uint64_t*)a <= *(uint64_t*)b;
        } else if (op == SEARCH_APPROX) {
            return *(uint64_t*)a == *(uint64_t*)b;
        }
    } else if (type == TYPE_FLOAT32) {
        if (op == SEARCH_EQUAL) {
            return *(float*)a == *(float*)b;
        } else if (op == SEARCH_GREATER) {
            return *(float*)a >= *(float*)b;
        } else if (op == SEARCH_LESS) {
            return *(float*)a <= *(float*)b;
        } else if (op == SEARCH_APPROX) {
            float a_value = *(float*)a;
            float b_value = *(float*)b;
            return (a_value >= b_value - 1.5f) && (a_value <= b_value + 1.5f);
        }
    } else if (type == TYPE_FLOAT64) {
        if (op == SEARCH_EQUAL) {
            return *(double*)a == *(double*)b;
        } else if (op == SEARCH_GREATER) {
            return *(double*)a >= *(double*)b;
        } else if (op == SEARCH_LESS) {
            return *(double*)a <= *(double*)b;
        } else if (op == SEARCH_APPROX) {
            double a_value = *(double*)a;
            double b_value = *(double*)b;
            return (a_value >= b_value - 1.5) && (a_value <= b_value + 1.5);
        }
    } else if (type == TYPE_BYTES_16) {
        if (op == SEARCH_EQUAL || op == SEARCH_APPROX) {
            return memcmp(a, b, 16) == 0;
        } else if (op == SEARCH_GREATER) {
            for (size_t i=0; i < 16 / sizeof(uint32_t); i++) {
                if (((uint32_t*)a)[i] > ((uint32_t*)b)[i]) {
                    return true;
                } else if (((uint32_t*)a)[i] < ((uint32_t*)b)[i]) {
                    return false;
                }
            }
            return false;
        } else if (op == SEARCH_LESS) {
            for (size_t i=0; i < 16 / sizeof(uint32_t); i++) {
                if (((uint32_t*)a)[i] < ((uint32_t*)b)[i]) {
                    return true;
                } else if (((uint32_t*)a)[i] > ((uint32_t*)b)[i]) {
                    return false;
                }
            }
            return false;
        }
    }

    return false;
}


#endif
