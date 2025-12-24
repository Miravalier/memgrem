#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include "subject.h"


typedef struct region
{
    size_t offset;
    size_t size;
    bool read;
    bool write;
    bool exec;
} region_t;



typedef struct maps {
    region_t *regions;
    size_t region_count;
} maps_t;


static int open_maps(pid_t pid) {
    char maps_path[32] = {0};
    snprintf(maps_path, 31, "/proc/%d/maps", pid);
    return open(maps_path, O_RDONLY);
}


static int memory_open(pid_t pid) {
    char memory_path[32] = {0};
    snprintf(memory_path, 31, "/proc/%d/mem", pid);
    return open(memory_path, O_RDWR);
}


static bool get_next_line(int fd, char *buffer, size_t buffer_size) {
    size_t line_length = 0;
    while (line_length < buffer_size - 1) {
        char c;
        if (read(fd, &c, 1) <= 0) {
            buffer[line_length] = '\0';
            return line_length > 0;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            buffer[line_length] = '\0';
            return true;
        }
        buffer[line_length++] = c;
    }
    // Line buffer full, discard until EOF or \n
    while (true) {
        char c;
        if (read(fd, &c, 1) <= 0) {
            break;
        }
        if (c == '\n') {
            break;
        }
    }
    buffer[line_length] = '\0';
    return true;
}


static maps_t *read_maps(pid_t pid) {
    maps_t *maps = calloc(1, sizeof(maps_t));
    size_t region_capacity = 32;
    maps->regions = malloc(region_capacity * sizeof(region_t));

    int fd = open_maps(pid);
    if (fd == -1) {
        fprintf(stderr, "error: failed to open /proc/<pid>/maps: %s\n", strerror(errno));
        free(maps);
        return NULL;
    }

    char line[1024] = {0};
    while (get_next_line(fd, line, sizeof(line) - 1)) {
        unsigned long start, end;
        char read, write, exec, cow;
        int offset, dev_major, dev_minor, inode;
        char filename[255] = {0};

        int scan_result = sscanf(
            line, "%lx-%lx %c%c%c%c %x %x:%x %u %[^\n]", &start, &end, &read,
            &write, &exec, &cow, &offset, &dev_major, &dev_minor, &inode, filename
        );
        if (scan_result < 6) {
            continue;
        }

        if (maps->region_count == region_capacity) {
            region_capacity *= 2;
            maps->regions = realloc(maps->regions, region_capacity * sizeof(region_t));
        }
        region_t *region = &maps->regions[maps->region_count++];
        region->offset = start;
        region->size = end - start;
        region->read = (read == 'r');
        region->write = (write == 'w');
        region->exec = (exec == 'x');

        if (!region->read || !region->write) {
            maps->region_count--;
        }
    }

    close(fd);
    return maps;
}


static void free_maps(maps_t *maps) {
    if (maps == NULL) {
        return;
    }
    free(maps->regions);
    free(maps);
}


static void generic_retrieve(scan_type_e type, void *dst, const void *src) {
    if (type == SCANTYPE_FLOAT32) {
        *(float*)dst = *(float*)src;
    } else if (type == SCANTYPE_FLOAT64) {
        *(double*)dst = *(double*)src;
    }
}


static bool generic_compare(scan_type_e type, search_op_e op, const void *a, const void *b) {
    if (op == SEARCH_NOOP) {
        return true;
    }

    if (type == SCANTYPE_FLOAT32) {
        if (op == SEARCH_EQUAL) {
            return *(float*)a == *(float*)b;
        } else if (op == SEARCH_GREATER) {
            return *(float*)a >= *(float*)b;
        } else if (op == SEARCH_LESS) {
            return *(float*)a <= *(float*)b;
        }
    } else if (type == SCANTYPE_FLOAT64) {
        if (op == SEARCH_EQUAL) {
            return *(double*)a == *(double*)b;
        } else if (op == SEARCH_GREATER) {
            return *(double*)a >= *(double*)b;
        } else if (op == SEARCH_LESS) {
            return *(double*)a <= *(double*)b;
        }
    }
    return false;
}


static bool memory_search(scan_t *scan, int fd, size_t offset, size_t size, void *needle, size_t needle_size, search_op_e op) {
    if (lseek(fd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "error: failed to lseek memory file: %s\n", strerror(errno));
        return false;
    }

    uint8_t buffer[65536];
    size_t bytes_remaining = size;
    while (bytes_remaining > 0) {
        size_t read_size = MIN(bytes_remaining, 65536);
        ssize_t read_result = read(fd, buffer, read_size);
        if (read_result < 0) {
            return false;
        }
        else if (read_result == 0) {
            break;
        }
        bytes_remaining -= (size_t)read_result;

        uint8_t *cursor = buffer;
        size_t cursor_size = (size_t)read_result;
        uint8_t *match;

        if (op == SEARCH_EQUAL) {
            while ((match = memmem(cursor, cursor_size, needle, needle_size))) {
                if (scan->hit_count == scan->hit_capacity) {
                    scan->hit_capacity *= 2;
                    scan->hits = realloc(scan->hits, scan->hit_capacity * sizeof(size_t));
                    if (scan->hits == NULL) {
                        return false;
                    }
                }
                scan->hits[scan->hit_count++] = offset + (match - buffer);
                cursor_size -= ((match + needle_size) - cursor);
                cursor = match + needle_size;
            }
        } else {
            for (size_t i=0; i + needle_size < cursor_size; i += needle_size) {
                if (generic_compare(scan->type, op, cursor + i, needle)) {
                    if (scan->hit_count == scan->hit_capacity) {
                        scan->hit_capacity *= 2;
                        scan->hits = realloc(scan->hits, scan->hit_capacity * sizeof(size_t));
                        if (scan->hits == NULL) {
                            return false;
                        }
                    }
                    scan->hits[scan->hit_count++] = offset + i;
                }
            }
        }

        offset += (size_t)read_result;
    }

    return true;
}


static bool memory_filter(scan_t *scan, int fd, void *value, size_t value_size, search_op_e op) {
    uint8_t buffer[sizeof(scan_value_u)];
    size_t old_hit_count = scan->hit_count;
    scan->hit_count = 0;
    for (size_t i=0; i < old_hit_count; i++) {
        size_t hit_location = scan->hits[i];
        lseek(fd, hit_location, SEEK_SET);
        read(fd, buffer, value_size);
        if (generic_compare(scan->type, op, buffer, value)) {
            if (scan->hit_count < 32) {
                generic_retrieve(scan->type, scan->values + scan->hit_count, buffer);
            }
            scan->hits[scan->hit_count++] = hit_location;
        }
    }
    return true;
}


static void push_scan(scan_t *scan) {
    scan->next = scan->subject->scans;
    scan->prev = NULL;
    scan->subject->scans = scan;
    if (scan->next != NULL) {
        scan->next->prev = scan;
    }
}


static void pop_scan(scan_t *scan) {
    if (scan->prev != NULL) {
        scan->prev->next = scan->next;
    }
    if (scan->next != NULL) {
        scan->next->prev = scan->prev;
    }
    if (scan->subject->scans == scan) {
        scan->subject->scans = scan->next;
    }
}


subject_t *subject_create(pid_t pid) {
    subject_t *subject = calloc(1, sizeof(subject_t));
    subject->pid = pid;

    if (ptrace(PTRACE_ATTACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
        subject_free(subject);
        return NULL;
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        subject_free(subject);
        subject = NULL;
    }

    if (ptrace(PTRACE_DETACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace detach: %s\n", strerror(errno));
        subject_free(subject);
        return NULL;
    }

    return subject;
}


scan_t *subject_begin_scan(subject_t *subject, scan_type_e type) {
    if (subject == NULL) {
        return NULL;
    }

    scan_t *scan = malloc(sizeof(scan_t));
    scan->subject = (subject_t *)subject;
    scan->type = type;
    scan->hits = NULL;
    scan->hit_count = 0;
    scan->hit_capacity = 0;

    push_scan(scan);
    return scan;
}


void subject_free(subject_t *subject) {
    if (subject == NULL) {
        return;
    }
    while (subject->scans != NULL) {
        scan_free(subject->scans);
    }
    free(subject);
}


size_t scan_type_size(scan_type_e type) {
    switch (type)
    {
        case SCANTYPE_UINT8: return sizeof(uint8_t);
        case SCANTYPE_UINT16: return sizeof(uint16_t);
        case SCANTYPE_UINT32: return sizeof(uint32_t);
        case SCANTYPE_UINT64: return sizeof(uint64_t);
        case SCANTYPE_INT8: return sizeof(int8_t);
        case SCANTYPE_INT16: return sizeof(int16_t);
        case SCANTYPE_INT32: return sizeof(int32_t);
        case SCANTYPE_INT64: return sizeof(int64_t);
        case SCANTYPE_FLOAT32: return sizeof(float);
        case SCANTYPE_FLOAT64: return sizeof(double);
    }
    return 0;
}


scan_t *scan_fork(scan_t *scan) {
    scan_t *result = malloc(sizeof(scan_t));
    memcpy(result, scan, sizeof(scan_t));
    result->hits = malloc(sizeof(size_t) * scan->hit_count);
    memcpy(result->hits, scan->hits, sizeof(size_t) * scan->hit_count);
    push_scan(result);
    return result;
}


bool scan_refresh(scan_t *scan) {
    int memory_fd = -1;
    bool success = false;
    bool attached = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;

    if (ptrace(PTRACE_ATTACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
        goto EXIT;
    }
    attached = true;

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        goto EXIT;
    }

    memory_fd = memory_open(pid);
    if (memory_fd == -1) {
        fprintf(stderr, "error: failed to open /proc/<pid>/mem: %s\n", strerror(errno));
        goto EXIT;
    }

    if (!memory_filter(scan, memory_fd, NULL, scan_type_size(scan->type), SEARCH_NOOP)) {
        goto EXIT;
    }

    success = true;

  EXIT:
    if (attached) {
        if (ptrace(PTRACE_DETACH, pid, 0L, 0L) == -1) {
            fprintf(stderr, "error: failed to ptrace detach: %s\n", strerror(errno));
            return false;
        }
    }
    if (memory_fd != -1) {
        close(memory_fd);
    }

    return success;
}


void scan_eliminate(scan_t *scan, size_t index) {
    if (index >= scan->hit_count) {
        return;
    }

    if (index < scan->hit_count - 1) {
        memmove(scan->hits + index, scan->hits + index + 1, (scan->hit_count - index) * sizeof(size_t));
    }
    scan->hit_count--;
}


bool scan_update(scan_t *scan, search_op_e op, ...) {
    int memory_fd = -1;
    bool success = false;
    bool attached = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;

    scan_value_u value;
    va_list args;
    va_start(args, op);

    switch (scan->type)
    {
        case SCANTYPE_UINT8: value.uint8 = (uint8_t)va_arg(args, unsigned); break;
        case SCANTYPE_UINT16: value.uint16 = (uint16_t)va_arg(args, unsigned); break;
        case SCANTYPE_UINT32: value.uint32 = va_arg(args, uint32_t); break;
        case SCANTYPE_UINT64: value.uint64 = va_arg(args, uint64_t); break;
        case SCANTYPE_INT8: value.int8 = (int8_t)va_arg(args, int); break;
        case SCANTYPE_INT16: value.int16 = (int16_t)va_arg(args, int); break;
        case SCANTYPE_INT32: value.int32 = va_arg(args, int32_t); break;
        case SCANTYPE_INT64: value.int64 = va_arg(args, int64_t); break;
        case SCANTYPE_FLOAT32: value.float32 = (float)va_arg(args, double); break;
        case SCANTYPE_FLOAT64: value.float64 = va_arg(args, double); break;
    }

    va_end(args);

    if (ptrace(PTRACE_ATTACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
        goto EXIT;
    }
    attached = true;

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        goto EXIT;
    }

    memory_fd = memory_open(pid);
    if (memory_fd == -1) {
        fprintf(stderr, "error: failed to open /proc/<pid>/mem: %s\n", strerror(errno));
        goto EXIT;
    }

    if (scan->hits == NULL) {
        maps_t *maps = read_maps(pid);
        if (maps == NULL) {
            goto EXIT;
        }

        scan->hit_capacity = 65536;
        scan->hits = malloc(scan->hit_capacity * sizeof(size_t));
        if (scan->hits == NULL) {
            fprintf(stderr, "error: failed to allocate 512KB: %s\n", strerror(errno));
            free_maps(maps);
            goto EXIT;
        }
        scan->hit_count = 0;
        for (size_t i=0; i < maps->region_count; i++) {
            region_t *region = &maps->regions[i];
            if (!memory_search(scan, memory_fd, region->offset, region->size, &value, scan_type_size(scan->type), op)) {
                free_maps(maps);
                goto EXIT;
            }
        }

        free_maps(maps);
    } else {
        if (!memory_filter(scan, memory_fd, &value, scan_type_size(scan->type), op)) {
            goto EXIT;
        }
    }

    success = true;

  EXIT:
    if (attached) {
        if (ptrace(PTRACE_DETACH, pid, 0L, 0L) == -1) {
            fprintf(stderr, "error: failed to ptrace detach: %s\n", strerror(errno));
            return false;
        }
    }
    if (memory_fd != -1) {
        close(memory_fd);
    }

    return success;
}


bool scan_set_value(scan_t *scan, ...) {
    int memory_fd = -1;
    bool success = false;
    bool attached = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;

    scan_value_u value;
    va_list args;
    va_start(args, scan);

    switch (scan->type)
    {
        case SCANTYPE_UINT8: value.uint8 = (uint8_t)va_arg(args, unsigned); break;
        case SCANTYPE_UINT16: value.uint16 = (uint16_t)va_arg(args, unsigned); break;
        case SCANTYPE_UINT32: value.uint32 = va_arg(args, uint32_t); break;
        case SCANTYPE_UINT64: value.uint64 = va_arg(args, uint64_t); break;
        case SCANTYPE_INT8: value.int8 = (int8_t)va_arg(args, int); break;
        case SCANTYPE_INT16: value.int16 = (int16_t)va_arg(args, int); break;
        case SCANTYPE_INT32: value.int32 = va_arg(args, int32_t); break;
        case SCANTYPE_INT64: value.int64 = va_arg(args, int64_t); break;
        case SCANTYPE_FLOAT32: value.float32 = (float)va_arg(args, double); break;
        case SCANTYPE_FLOAT64: value.float64 = va_arg(args, double); break;
    }

    va_end(args);

    if (ptrace(PTRACE_ATTACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
        goto EXIT;
    }
    attached = true;

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        goto EXIT;
    }

    memory_fd = memory_open(pid);
    if (memory_fd == -1) {
        fprintf(stderr, "error: failed to open /proc/<pid>/mem: %s\n", strerror(errno));
        goto EXIT;
    }

    size_t value_size = scan_type_size(scan->type);
    for (size_t i=0; i < scan->hit_count; i++) {
        size_t hit = scan->hits[i];
        lseek(memory_fd, hit, SEEK_SET);
        write(memory_fd, &value, value_size);
    }

    success = true;

  EXIT:
    if (attached) {
        if (ptrace(PTRACE_DETACH, pid, 0L, 0L) == -1) {
            fprintf(stderr, "error: failed to ptrace detach: %s\n", strerror(errno));
            return false;
        }
    }
    if (memory_fd != -1) {
        close(memory_fd);
    }

    return success;
}


void scan_print(scan_t *scan) {
    if (scan->hit_count == 0) {
        printf("[0 hits] (No values matched)\n");
    } else if(scan->hit_count == 1) {
        printf("[1 hit]: 0x%lx\n", scan->hits[0]);
    } else if (scan->hit_count < 32) {
        printf("[%zu hits]:\n", scan->hit_count);
        for (size_t i=0; i < scan->hit_count; i++) {
            printf("0x%lx\n", scan->hits[i]);
        }
    } else {
        printf("[%zu hits] (Too many to list)\n", scan->hit_count);
    }
}


void scan_free(scan_t *scan) {
    if (scan == NULL) {
        return;
    }

    pop_scan(scan);

    if (scan->hits != NULL) {
        free(scan->hits);
    }
    free(scan);
}
