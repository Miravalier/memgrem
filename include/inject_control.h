#ifndef _INJECT_CONTROL_H
#define _INJECT_CONTROL_H

#include <stdint.h>

#define INJECT_MAGIC_A   0x0773bd85e714097e
#define INJECT_MAGIC_B   0x814af3aa2fa912df
#define INJECT_MAGIC_XOR 0x86394e2fc8bd1ba1

extern uint8_t *control_buffer;

void wait_for_canaries(void);

#endif
