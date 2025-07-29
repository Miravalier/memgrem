#ifndef _SUBJECT_H
#define _SUBJECT_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>


typedef enum scan_type {
    SCANTYPE_UINT8,
    SCANTYPE_UINT16,
    SCANTYPE_UINT32,
    SCANTYPE_UINT64,
    SCANTYPE_INT8,
    SCANTYPE_INT16,
    SCANTYPE_INT32,
    SCANTYPE_INT64,
    SCANTYPE_FLOAT32,
    SCANTYPE_FLOAT64,
} scan_type_e;


typedef union scan_value {
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
} scan_value_u;


typedef struct subject {
    pid_t pid;
    pthread_t thread_id;
    struct scan *scans;
} subject_t;


typedef struct scan {
    struct subject *subject;
    scan_type_e type;
    size_t *hits;
    size_t hit_count;
    size_t hit_capacity;
    struct scan *next;
    struct scan *prev;
} scan_t;


subject_t *subject_create(pid_t pid);
scan_t *subject_begin_scan(subject_t *subject, scan_type_e type);
void subject_free(subject_t *subject);

scan_t *scan_fork(scan_t *scan);
bool scan_set_value(scan_t *scan, ...);
bool scan_update(scan_t *scan, ...);
void scan_print(scan_t *scan);
void scan_free(scan_t *scan);


#endif
