#ifndef _SUBJECT_H
#define _SUBJECT_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "inject_control.h"
#include "gval.h"


typedef struct subject {
    pid_t pid;
    pthread_t thread_id;
    struct scan *scans;
    int attached;
    int memory_fd;
    uintptr_t control_buffer_address;
} subject_t;


typedef struct scan {
    struct subject *subject;
    gval_type_e type;
    size_t *hits;
    size_t hit_count;
    size_t hit_capacity;
    gval_u values[32];
    struct scan *next;
    struct scan *prev;
} scan_t;


subject_t *subject_create(pid_t pid);
bool subject_attach(subject_t *subject);
void subject_detach(subject_t *subject);
void subject_free(subject_t *subject);
bool subject_inject_syscall0(subject_t *subject, uintptr_t *result, int syscall);
bool subject_inject_syscall1(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1);
bool subject_inject_syscall2(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2);
bool subject_inject_syscall3(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3);
bool subject_inject_syscall4(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4);
bool subject_inject_syscall5(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5);
bool subject_inject_syscall6(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6);
bool subject_inject_so(subject_t *subject, const char *so_path);
bool subject_inject_worker(subject_t *subject);

bool subject_command_sw_lock(subject_t *subject, lock_t *lock);
bool subject_command_sw_unlock(subject_t *subject, uintptr_t addr);
bool subject_command_print(subject_t *subject, const char *message);

scan_t *subject_begin_scan(subject_t *subject, gval_type_e type);
scan_t *scan_fork(scan_t *scan);
bool scan_set_value(scan_t *scan, ...);
bool scan_update(scan_t *scan, search_op_e op, ...);
void scan_eliminate(scan_t *scan, size_t index);
bool scan_refresh(scan_t *scan);
void scan_print(scan_t *scan);
void scan_free(scan_t *scan);
void scan_reset(scan_t *scan);
size_t scan_value_size(scan_t *scan);


#endif
