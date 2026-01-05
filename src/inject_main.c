#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ucontext.h>

#include <xed/xed-interface.h>

#include "gval.h"
#include "inject_control.h"
#include "utils.h"

extern control_buffer_t *control_buffer;
xed_state_t xed_state;


lock_t sw_locks[256];
size_t sw_lock_count = 0;
uintptr_t hw_locks[256];
size_t hw_lock_count = 0;


static size_t decode_instruction_length(const void *addr) {
    xed_decoded_inst_t decoded_instruction;
    xed_decoded_inst_zero_set_mode(&decoded_instruction, &xed_state);
    if (xed_ild_decode(&decoded_instruction, addr, XED_MAX_INSTRUCTION_BYTES) != XED_ERROR_NONE) {
        return 0;
    }
    return (size_t)xed_decoded_inst_get_length(&decoded_instruction);
}


static bool is_write_blocked(void *addr) {
    return true;
}


static void segfault_handler(int signo, siginfo_t *si, void *data) {
    ucontext_t *ctx = (ucontext_t *)data;
    void *fault_addr = (void *)si->si_addr;
    const void *rip_addr = (const void *)(uintptr_t)ctx->uc_mcontext.gregs[REG_RIP];

    intptr_t instruction_length = decode_instruction_length(rip_addr);
    ctx->uc_mcontext.gregs[REG_RIP] += instruction_length;

    if (!is_write_blocked(fault_addr)) {
        // Do the write somehow ...
    }
}


static void handle_request(void) {
    switch (control_buffer->request) {
        case CONTROL_REQUEST_PRINT: {
            const char *message = control_buffer->string_arg;
            if (*message != '\0') {
                printf("%s\n", message);
            } else {
                printf("Received empty print request\n");
            }
        }
        break;
        case CONTROL_REQUEST_SW_LOCK: {
            bool existing_lock = false;
            for (size_t i=0; i < sw_lock_count; i++) {
                if (sw_locks[i].location == control_buffer->lock_arg.location) {
                    memcpy(sw_locks + i, &control_buffer->lock_arg, sizeof(lock_t));
                    existing_lock = true;
                    break;
                }
            }
            if (!existing_lock) {
                memcpy(sw_locks + sw_lock_count, &control_buffer->lock_arg, sizeof(lock_t));
                sw_lock_count++;
            }
        }
        break;
        case CONTROL_REQUEST_SW_UNLOCK: {
            uintptr_t addr_to_unlock = control_buffer->lock_arg.location;
            size_t previous_lock_count = sw_lock_count;
            sw_lock_count = 0;
            if (addr_to_unlock != 0) {
                for (size_t i=0; i < previous_lock_count; i++) {
                    lock_t *previous_lock = sw_locks + i;
                    if (previous_lock->location != addr_to_unlock) {
                        memcpy(sw_locks + sw_lock_count, previous_lock, sizeof(lock_t));
                        sw_lock_count++;
                    }
                }
            }
        }
        break;
        case CONTROL_REQUEST_HW_LOCK: {
            uintptr_t addr_to_lock = control_buffer->args[0];
            uintptr_t page_to_lock = addr_to_lock & 0xfffffffffffff000;
            hw_locks[hw_lock_count++] = addr_to_lock;
            mprotect((void *)page_to_lock, 4096, PROT_READ);
        }
        break;
        case CONTROL_REQUEST_HW_UNLOCK: {
            bool unlock_page = true;
            uintptr_t addr_to_unlock = control_buffer->args[0];
            uintptr_t page_to_unlock = addr_to_unlock & 0xfffffffffffff000;
            size_t previous_address_count = hw_lock_count;
            hw_lock_count = 0;
            for (size_t i=0; i < previous_address_count; i++) {
                uintptr_t previous_addr = hw_locks[i];
                if (previous_addr != addr_to_unlock) {
                    hw_locks[hw_lock_count++] = previous_addr;
                    uintptr_t previous_page = previous_addr & 0xfffffffffffff000;
                    if (previous_page == page_to_unlock) {
                        unlock_page = false;
                    }
                }
            }
            if (unlock_page) {
                mprotect((void *)addr_to_unlock, 4096, PROT_READ|PROT_WRITE);
            }
        }
        break;
    }
}


static void *inject_worker(void *_arg) {
    (void)_arg;

    while (true) {
        ms_sleep(25);
        if (control_buffer->inbound) {
            control_buffer->inbound = 0;
            handle_request();
            control_buffer->outbound = 1;
        }
        for (size_t i=0; i < sw_lock_count; i++) {
            lock_t lock = sw_locks[i];
            gval_move(lock.type, (void *)lock.location, &lock.value);
        }
    }

    return NULL;
}


__attribute__((constructor))
void inject_main(void)
{
    xed_tables_init();
    xed_state_zero(&xed_state);
    xed_state.mmode = XED_MACHINE_MODE_LONG_64;

    struct sigaction segfault_sigaction = {
        .sa_flags = SA_ONSTACK | SA_RESTART | SA_SIGINFO,
        .sa_sigaction = segfault_handler,
    };

    if (sigaction(SIGSEGV, &segfault_sigaction, NULL) == -1) {
        return;
    }

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, (void*(*)(void *))inject_worker, NULL) != 0) {
        return;
    }
    pthread_detach(thread_id);
}
