#include "gval.h"


size_t gval_size(gval_type_e type)
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

void gval_move(gval_type_e type, void *dst, const void *src)
{
    if (type == TYPE_FLOAT32) {
        *(float*)dst = *(float*)src;
    } else if (type == TYPE_FLOAT64) {
        *(double*)dst = *(double*)src;
    }
}


bool gval_compare(gval_type_e type, search_op_e op, const void *a, const void *b)
{
    if (op == SEARCH_NOOP) {
        return true;
    }

    if (type == TYPE_FLOAT32) {
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
    }

    return false;
}
