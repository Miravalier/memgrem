#include <time.h>
#include <stdint.h>

#include "inject_control.h"
#include "utils.h"


unsigned long canaries[512] = {
    INJECT_MAGIC_A,
    INJECT_MAGIC_B,
};


uint8_t *control_buffer = (uint8_t *)canaries;


void wait_for_canaries(void) {
    while ((canaries[0] ^ canaries[1]) == INJECT_MAGIC_XOR) {
        ms_sleep(100);
    }
}
