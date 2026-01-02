#include <time.h>
#include <stdint.h>

#include "inject_control.h"
#include "utils.h"


uint64_t control_buffer_storage[512] = {
    INJECT_MAGIC_A,
    INJECT_MAGIC_B,
};


control_buffer_t *control_buffer = (control_buffer_t *)control_buffer_storage;
