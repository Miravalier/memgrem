#ifndef _INJECT_CONTROL_H
#define _INJECT_CONTROL_H

#include <stdint.h>
#include "gval.h"

#define INJECT_MAGIC_A   0x0773bd85e714097e
#define INJECT_MAGIC_B   0x814af3aa2fa912df

#define CONTROL_REQUEST_NONE        0
#define CONTROL_REQUEST_PRINT       1
#define CONTROL_REQUEST_HW_LOCK     2
#define CONTROL_REQUEST_HW_UNLOCK   3
#define CONTROL_REQUEST_SW_LOCK     4
#define CONTROL_REQUEST_SW_UNLOCK   5

typedef struct lock {
    uintptr_t location;
    gval_type_e type;
    gval_u value;
} lock_t;

typedef struct control_buffer {
    uint64_t magic_a;
    uint64_t magic_b;
    uint64_t inbound;
    uint64_t outbound;
    uint64_t request;
    uint64_t response;
    uint64_t args[8];
    char string_arg[256];
    uint8_t bin_arg[256];
    lock_t lock_arg;
} control_buffer_t;

#endif
