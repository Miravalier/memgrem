#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>

#include "inject_control.h"
#include "string_list.h"
#include "subject.h"
#include "utils.h"


#define SYSCALL_READ    0
#define SYSCALL_WRITE   1
#define SYSCALL_OPEN    2
#define SYSCALL_CLOSE   3
#define SYSCALL_MMAP    9
#define SYSCALL_MUNMAP  11
#define SYSCALL_MEMFD   319

#define PERM_READ       4
#define PERM_WRITE      2
#define PERM_EXEC       1


typedef struct region
{
    size_t offset;
    size_t size;
    bool read;
    bool write;
    bool exec;
    char filename[256];
} region_t;


typedef struct maps {
    region_t *regions;
    size_t region_count;
} maps_t;


static int maps_fd_open(pid_t pid) {
    if (pid == 0) {
        return open("/proc/self/maps", O_RDONLY);
    }
    char maps_path[32] = {0};
    snprintf(maps_path, 31, "/proc/%d/maps", pid);
    return open(maps_path, O_RDONLY);
}


static int memory_fd_open(pid_t pid) {
    char memory_path[32] = {0};
    snprintf(memory_path, 31, "/proc/%d/mem", pid);
    return open(memory_path, O_RDWR);
}


static const char *syscall_strerror(uintptr_t rax) {
    int err_code = (int)(-(intptr_t)rax);
    return strerror(err_code);
}


bool read_file(const char *name, uint8_t **contents_out, size_t *size_out)
{
    bool success = false;
    uint8_t *contents = NULL;
    size_t capacity = 4096;
    size_t filesize = 0;
    int fd = -1;

    fd = open(name, O_RDONLY);
    if (fd == -1) {
        goto CLEANUP;
    }

    contents = malloc(capacity);
    if (contents == NULL) {
        fprintf(stderr, "error: out of memory while allocating file contents capacity\n");
        goto CLEANUP;
    }

    ssize_t bytes_read;
    do {
        if (capacity - filesize < 4096) {
            capacity *= 2;
            uint8_t *resized_buffer = realloc(contents, capacity);
            if (resized_buffer == NULL) {
                fprintf(stderr, "error: out of memory while growing file contents capacity\n");
                goto CLEANUP;
            }
            contents = resized_buffer;
        }
        bytes_read = read(fd, contents + filesize, capacity - (filesize + 1));
        if (bytes_read < 0) {
            goto CLEANUP;
        }
        filesize += (ssize_t)bytes_read;
    } while (bytes_read > 0);
    contents[filesize] = '\0';

    success = true;

  CLEANUP:
    if (fd != -1) {
        close(fd);
    }

    if (success) {
        *contents_out = contents;
        *size_out = filesize;
    } else {
        *contents_out = NULL;
        *size_out = 0;
        if (contents != NULL) {
            free(contents);
        }
    }

    return success;
}


static bool memory_write(int fd, const void *buffer, uintptr_t addr, size_t length) {
    if (lseek(fd, addr, SEEK_SET) == -1) {
        fprintf(stderr, "error: failed to lseek() in memory file: %s\n", strerror(errno));
        return false;
    }
    size_t bytes_written = 0;
    while (bytes_written < length) {
        ssize_t last_write = write(fd, buffer+bytes_written, length-bytes_written);
        if (last_write <= 0) {
            fprintf(stderr, "error: memory write failed: %s\n", strerror(errno));
            return false;
        }
        bytes_written += (size_t)last_write;
    }
    return true;
}


static char *memory_read_string(int fd, uintptr_t addr) {
    if (lseek(fd, addr, SEEK_SET) == -1) {
        fprintf(stderr, "error: failed to lseek() in memory file: %s\n", strerror(errno));
        return false;
    }

    size_t length = 0;
    size_t capacity = 256;
    char *result = malloc(capacity);
    while (read(fd, result + length, 1) > 0) {
        if (length == capacity - 1) {
            capacity *= 2;
            result = realloc(result, capacity);
        }
        if (result[length++] == '\0') {
            break;
        }
    }
    result[length] = '\0';
    return result;
}


static bool memory_read(int fd, void *buffer, uintptr_t addr, size_t length) {
    if (lseek(fd, addr, SEEK_SET) == -1) {
        fprintf(stderr, "error: failed to lseek() in memory file: %s\n", strerror(errno));
        return false;
    }
    size_t bytes_read = 0;
    while (bytes_read < length) {
        ssize_t last_read = read(fd, buffer+bytes_read, length-bytes_read);
        if (last_read <= 0) {
            fprintf(stderr, "error: memory read failed: %s\n", strerror(errno));
            return false;
        }
        bytes_read += (size_t)last_read;
    }
    return true;
}


static void memory_debug_print(int fd, uintptr_t addr, size_t length) {
    uint8_t *buffer = malloc(length);
    if (memory_read(fd, buffer, addr, length)) {
        printf("DBG (%p): ", (void*)addr);
        for (size_t i=0; i < length; i++) {
            printf("%02x", buffer[i]);
            if (i < length - 1) {
                printf(" ");
            }
        }
        printf("\n");
    } else {
        printf("DBG: could not read subject memory at %p\n", (void*)addr);
    }
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

    int fd = maps_fd_open(pid);
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
        char filename[256] = {0};

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
        strcpy(region->filename, filename);
    }

    close(fd);
    return maps;
}


static region_t *find_region(maps_t *maps, const char *filename, int permissions) {
    const char *current_filename = "";
    for (size_t i=0; i < maps->region_count; i++) {
        region_t *region = maps->regions + i;
        if (strlen(region->filename) > 0) {
            current_filename = region->filename;
        }
        int region_permissions = (region->read ? PERM_READ : 0)|(region->write ? PERM_WRITE : 0)|(region->exec ? PERM_EXEC : 0);
        if (str_contains(current_filename, filename) && region_permissions == permissions) {
            return region;
        }
    }
    return NULL;
}


static void print_maps(maps_t *maps) {
    printf("Offset           Size     RWX Name\n");
    for (size_t i=0; i < maps->region_count; i++) {
        region_t *region = maps->regions + i;
        printf("%016zx %08zx %c%c%c %s\n", region->offset, region->size, (region->read?'r':'-'), (region->write?'w':'-'), (region->exec?'x':'-'), region->filename);
    }
}


static void free_maps(maps_t *maps) {
    if (maps == NULL) {
        return;
    }
    free(maps->regions);
    free(maps);
}


static bool memory_search(scan_t *scan, gval_type_e type, size_t offset, size_t size, gval_u *value, search_op_e op) {
    int fd = scan->subject->memory_fd;
    size_t value_size = gval_size(type);

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
            while ((match = memmem(cursor, cursor_size, value, value_size))) {
                if (scan->hit_count < 32) {
                    gval_move(type, scan->values + scan->hit_count, match);
                }
                if (scan->hit_count == scan->hit_capacity) {
                    scan->hit_capacity *= 2;
                    scan->hits = realloc(scan->hits, scan->hit_capacity * sizeof(size_t));
                    if (scan->hits == NULL) {
                        return false;
                    }
                }
                scan->hits[scan->hit_count++] = offset + (match - buffer);
                cursor_size -= ((match + value_size) - cursor);
                cursor = match + value_size;
            }
        } else {
            for (size_t i=0; i + value_size < cursor_size; i += value_size) {
                if (gval_compare(type, op, cursor + i, value)) {
                    if (scan->hit_count < 32) {
                        gval_move(type, scan->values + scan->hit_count, cursor + i);
                    }
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


static bool memory_filter(scan_t *scan, gval_type_e type, gval_u *value, search_op_e op) {
    int fd = scan->subject->memory_fd;
    size_t value_size = gval_size(type);

    uint8_t buffer[sizeof(gval_u)];
    size_t old_hit_count = scan->hit_count;
    scan->hit_count = 0;
    for (size_t i=0; i < old_hit_count; i++) {
        size_t hit_location = scan->hits[i];
        lseek(fd, hit_location, SEEK_SET);
        read(fd, buffer, value_size);
        if (gval_compare(type, op, buffer, value)) {
            if (scan->hit_count < 32) {
                gval_move(type, scan->values + scan->hit_count, buffer);
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


static bool subject_continue_until_breakpoint(subject_t *subject) {
    int status;

    // Allow the subject to continue executing
    if (ptrace(PTRACE_CONT, subject->pid, NULL, NULL) == -1) {
        fprintf(stderr, "error: failed to PTRACE_CONT: %s\n", strerror(errno));
        return false;
    }

    // Wait until our breakpoint is hit
    if (waitpid(subject->pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        return false;
    }
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "error: failed to waitpid, subject did not stop\n");
        return false;
    }
    if (WSTOPSIG(status) != SIGTRAP) {
        fprintf(stderr, "error: subject did not hit SIGTRAP, hit signal %d instead\n", WSTOPSIG(status));
        return false;
    }

    return true;
}


void subject_detach(subject_t *subject) {
    if (subject->attached == 0) {
        return;
    } else if (subject->attached > 1) {
        subject->attached--;
    } else {
        if (ptrace(PTRACE_DETACH, subject->pid, 0L, 0L) == -1) {
            fprintf(stderr, "warning: failed to ptrace detach: %s\n", strerror(errno));
        }

        if (subject->memory_fd != -1) {
            close(subject->memory_fd);
            subject->memory_fd = -1;
        }

        subject->attached = 0;
    }
}


bool subject_attach(subject_t *subject) {
    pid_t pid = subject->pid;

    if (subject->attached > 0) {
        subject->attached++;
        return true;
    }

    if (ptrace(PTRACE_ATTACH, pid, 0L, 0L) == -1) {
        fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
        return false;
    }
    subject->attached = 1;

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        subject_detach(subject);
        return false;
    }

    subject->memory_fd = memory_fd_open(pid);
    if (subject->memory_fd == -1) {
        fprintf(stderr, "error: failed to open memory fd: %s\n", strerror(errno));
        subject_detach(subject);
        return false;
    }

    return true;
}


subject_t *subject_create(pid_t pid) {
    subject_t *subject = calloc(1, sizeof(subject_t));
    if (subject == NULL) {
        return NULL;
    }

    subject->pid = pid;
    subject->attached = 0;
    subject->memory_fd = -1;

    if (!subject_attach(subject)) {
        subject_free(subject);
        return NULL;
    }

    return subject;
}


static bool subject_issue_command(subject_t *subject, control_buffer_t *control_buffer) {
    if (subject->control_buffer_address == 0) {
        if (!subject_inject_worker(subject)) {
            fprintf(stderr, "error: failed to inject worker thread\n");
            return false;
        }
    }

    bool success = false;
    control_buffer->inbound = 1;
    control_buffer->magic_a = INJECT_MAGIC_A;
    control_buffer->magic_b = INJECT_MAGIC_B;

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    if (!memory_write(subject->memory_fd, control_buffer, subject->control_buffer_address, sizeof(control_buffer_t))) {
        goto EXIT;
    }

    if (ptrace(PTRACE_DETACH, subject->pid, 0L, 0L) == -1) {
        fprintf(stderr, "warning: failed to ptrace detach: %s\n", strerror(errno));
        goto EXIT;
    }

    // Wait up to 5 seconds for the worker to finish the request
    for (size_t i=0; i < 50; i++) {
        ms_sleep(100);
        if (ptrace(PTRACE_ATTACH, subject->pid, 0L, 0L) == -1) {
            fprintf(stderr, "error: failed to ptrace attach: %s\n", strerror(errno));
            goto EXIT;
        }

        int status;
        if (waitpid(subject->pid, &status, 0) == -1) {
            fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
            goto EXIT;
        }

        if (!memory_read(subject->memory_fd, control_buffer, subject->control_buffer_address, sizeof(control_buffer_t))) {
            goto EXIT;
        }

        if (control_buffer->outbound) {
            break;
        }

        if (ptrace(PTRACE_DETACH, subject->pid, 0L, 0L) == -1) {
            fprintf(stderr, "error: failed to ptrace detach: %s\n", strerror(errno));
            goto EXIT;
        }
    }

    if (!control_buffer->outbound) {
        fprintf(stderr, "error: worker request timed out, worker may be dead\n");
        goto EXIT;

    }

    success = true;

  EXIT:
    subject_detach(subject);
    return success;
}


static bool subject_inject_syscall(subject_t *subject, uintptr_t *result,
        int syscall, uintptr_t rdi, uintptr_t rsi, uintptr_t rdx, uintptr_t r10, uintptr_t r8, uintptr_t r9)
{
    bool success = false;
    pid_t pid = subject->pid;
    int memory_fd = subject->memory_fd;
    int status;

    uint8_t injected_code[] = {
        0x90,       // nop
        0x90,       // nop
        0x0f, 0x05, // syscall
        0xcc,       // int 3
    };
    uint8_t backup_code[sizeof(injected_code)];

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    struct user_regs_struct starting_registers;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &starting_registers) == -1) {
        fprintf(stderr, "error: failed to ptrace GETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    // Save the code currently under RIP
    if (!memory_read(memory_fd, backup_code, starting_registers.rip, sizeof(backup_code))) {
        goto EXIT;
    }

    // Write new code to RIP
    if (!memory_write(memory_fd, injected_code, starting_registers.rip, sizeof(injected_code))) {
        goto EXIT;
    }

    // Set registers up for syscall
    struct user_regs_struct registers;
    memcpy(&registers, &starting_registers, sizeof(struct user_regs_struct));
    registers.rip += 2;
    registers.rax = (unsigned long long)syscall;
    registers.rdi = rdi;
    registers.rsi = rsi;
    registers.rdx = rdx;
    registers.r10 = r10;
    registers.r8 = r8;
    registers.r9 = r9;
    if (ptrace(PTRACE_SETREGS, pid, NULL, &registers) == -1) {
        fprintf(stderr, "error: failed to ptrace SETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    // Allow the subject to continue executing
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        fprintf(stderr, "error: failed to PTRACE_CONT: %s\n", strerror(errno));
        goto EXIT;
    }

    // Wait until our breakpoint is hit
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "error: failed to waitpid: %s\n", strerror(errno));
        goto EXIT;
    }
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "error: subject did not stop\n");
        goto EXIT;
    }
    if (WSTOPSIG(status) != SIGTRAP) {
        fprintf(stderr, "error: subject did not hit SIGTRAP, hit %d instead\n", WSTOPSIG(status));
        goto EXIT;
    }

    // Retrieve result
    if (ptrace(PTRACE_GETREGS, pid, NULL, &registers) == -1) {
        fprintf(stderr, "error: failed to ptrace GETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    if (result) {
        *result = registers.rax;
    }

    // Restore backup code to RIP
    if (!memory_write(memory_fd, backup_code, starting_registers.rip, sizeof(backup_code))) {
        goto EXIT;
    }

    // Restore original registers
    if (ptrace(PTRACE_SETREGS, pid, NULL, &starting_registers) == -1) {
        fprintf(stderr, "error: failed to ptrace SETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    success = true;

  EXIT:
    subject_detach(subject);
    return success;
}


bool subject_inject_syscall0(subject_t *subject, uintptr_t *result, int syscall) {
    return subject_inject_syscall(subject, result, syscall, 0, 0, 0, 0, 0, 0);
}


bool subject_inject_syscall1(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1) {
    return subject_inject_syscall(subject, result, syscall, arg1, 0, 0, 0, 0, 0);
}


bool subject_inject_syscall2(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2) {
    return subject_inject_syscall(subject, result, syscall, arg1, arg2, 0, 0, 0, 0);
}


bool subject_inject_syscall3(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    return subject_inject_syscall(subject, result, syscall, arg1, arg2, arg3, 0, 0, 0);
}


bool subject_inject_syscall4(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
    return subject_inject_syscall(subject, result, syscall, arg1, arg2, arg3, arg4, 0, 0);
}


bool subject_inject_syscall5(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5) {
    return subject_inject_syscall(subject, result, syscall, arg1, arg2, arg3, arg4, arg5, 0);
}


bool subject_inject_syscall6(subject_t *subject, uintptr_t *result, int syscall, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6) {
    return subject_inject_syscall(subject, result, syscall, arg1, arg2, arg3, arg4, arg5, arg6);
}


bool subject_inject_worker(subject_t *subject) {
    bool success = false;
    unsigned long magic_bytes[] = {
        INJECT_MAGIC_A,
        INJECT_MAGIC_B,
    };

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    // Look for existing magic values from a previous injection
    scan_t *magic_scan = subject_begin_scan(subject);
    if (!scan_update(magic_scan, TYPE_BYTES_16, SEARCH_EQUAL, magic_bytes)) {
        fprintf(stderr, "error: could not start scan before injecting shared object\n");
        goto EXIT;
    }

    if (magic_scan->hit_count == 1) {
        subject->control_buffer_address = (uintptr_t)magic_scan->hits[0];
        success = true;
        goto EXIT;
    }

    // Inject the worker shared object into the subject
    char exe_path[256] = {0};
    if (readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1) == -1) {
        fprintf(stderr, "error: failed to call readlink(\"/proc/self/exe\"): %s\n", strerror(errno));
        goto EXIT;
    }

    char *exe_dir = dirname(exe_path);
    char so_path[256] = {0};
    strcat(so_path, exe_dir);
    strcat(so_path, "/inject.so");

    if (!subject_inject_so(subject, so_path)) {
        fprintf(stderr, "error: failed to inject shared object\n");
        goto EXIT;
    }

    // Look for the magic values again after injecting
    scan_reset(magic_scan);
    if (!scan_update(magic_scan, TYPE_BYTES_16, SEARCH_EQUAL, magic_bytes)) {
        fprintf(stderr, "error: could not start scan after injecting shared object\n");
        goto EXIT;
    }

    if (magic_scan->hit_count != 1) {
        fprintf(stderr, "error: could not find magic bytes after injecting shared object\n");
        goto EXIT;
    }

    subject->control_buffer_address = (uintptr_t)magic_scan->hits[0];
    success = true;

  EXIT:
    subject_detach(subject);
    return success;
}


bool subject_command_sw_lock(subject_t *subject, lock_t *lock) {
    control_buffer_t control_buffer = {
        .request = CONTROL_REQUEST_SW_LOCK,
    };
    memcpy(&control_buffer.lock_arg, lock, sizeof(lock_t));
    return subject_issue_command(subject, &control_buffer);
}


bool subject_command_sw_unlock(subject_t *subject, uintptr_t addr) {
    control_buffer_t control_buffer = {
        .request = CONTROL_REQUEST_SW_LOCK,
        .lock_arg = {
            .location = addr,
        },
    };
    return subject_issue_command(subject, &control_buffer);
}


bool subject_command_print(subject_t *subject, const char *message) {
    control_buffer_t control_buffer = {
        .request = CONTROL_REQUEST_PRINT,
    };

    if (message != NULL) {
        strncpy(control_buffer.string_arg, message, 255);
        control_buffer.string_arg[255] = '\0';
    }

    return subject_issue_command(subject, &control_buffer);
}


bool subject_inject_so(subject_t *subject, const char *so_path) {
    bool success = false;
    uint8_t *so_file_contents = NULL;
    size_t so_file_size = 0;
    int status;
    uintptr_t result;
    struct user_regs_struct starting_registers;
    bool registers_saved = false;

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    pid_t pid = subject->pid;
    int memory_fd = subject->memory_fd;

    if (!read_file(so_path, &so_file_contents, &so_file_size)) {
        fprintf(stderr, "error: failed to read so from \"%s\"\n", so_path);
        goto EXIT;
    }

    // Find our own offset from libc to dlopen and dlerror
    maps_t *self_maps = read_maps(0);
    region_t *self_libc_code_region = find_region(self_maps, "libc.so", PERM_READ|PERM_EXEC);
    if (self_libc_code_region == NULL) {
        fprintf(stderr, "error: failed to find libc.so in /proc/self/maps\n");
        goto EXIT;
    }
    uintptr_t self_dlopen_address = (uintptr_t)dlsym(RTLD_DEFAULT, "dlopen");
    uintptr_t self_dlerror_address = (uintptr_t)dlsym(RTLD_DEFAULT, "dlerror");
    uintptr_t dlopen_offset = self_dlopen_address - self_libc_code_region->offset;
    uintptr_t dlerror_offset = self_dlerror_address - self_libc_code_region->offset;
    free_maps(self_maps);

    // Find the subject's dlopen from their libc base
    maps_t *subject_maps = read_maps(pid);
    region_t *subject_libc_code_region = find_region(subject_maps, "libc.so", PERM_READ|PERM_EXEC);
    if (subject_libc_code_region == NULL) {
        fprintf(stderr, "error: failed to find libc.so in subject's /proc/<pid>/maps\n");
        goto EXIT;
    }
    uintptr_t subject_dlopen_address = subject_libc_code_region->offset + dlopen_offset;
    uintptr_t subject_dlerror_address = subject_libc_code_region->offset + dlerror_offset;
    free_maps(subject_maps);

    // Save subject's starting registers
    if (ptrace(PTRACE_GETREGS, pid, NULL, &starting_registers) == -1) {
        fprintf(stderr, "error: failed to ptrace GETREGS: %s\n", strerror(errno));
        goto EXIT;
    }
    registers_saved = true;

    // Create mmap region
    if (!subject_inject_syscall6(subject, &result, SYSCALL_MMAP, 0, 8192, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) {
        fprintf(stderr, "error: mmap syscall injection failed\n");
        goto EXIT;
    }
    if ((intptr_t)result < 0) {
        fprintf(stderr, "error: subject mmap() failed: %s\n", syscall_strerror(result));
        goto EXIT;
    }
    uintptr_t subject_mmap_region = result;
    uintptr_t subject_code_region = subject_mmap_region + 0;
    uintptr_t subject_data_region = subject_code_region + 4096;

    // Write the tmp filename to the subject mmap'd area for open()
    const char *tmp_filename = "/tmp/memgrem-inject.so";
    if (!memory_write(memory_fd, tmp_filename, subject_data_region, strlen(tmp_filename) + 1)) {
        goto EXIT;
    }

    // Create the so file in the subject's filesystem
    if (!subject_inject_syscall3(subject, &result, SYSCALL_OPEN, subject_data_region, O_CREAT|O_TRUNC|O_WRONLY, 0664)) {
        fprintf(stderr, "error: open syscall injection failed\n");
        goto EXIT;
    }
    if ((intptr_t)result < 0) {
        fprintf(stderr, "error: subject open() failed: %s\n", syscall_strerror(result));
        goto EXIT;
    }
    int subject_fd = (int)result;

    // Write the contents of the file page by page
    size_t bytes_written = 0;
    while (bytes_written < so_file_size) {
        size_t chunk_size = MIN(4096, so_file_size - bytes_written);
        if (!memory_write(memory_fd, so_file_contents + bytes_written, subject_data_region, chunk_size)) {
            goto EXIT;
        }

        if (!subject_inject_syscall3(subject, &result, SYSCALL_WRITE, subject_fd, subject_data_region, chunk_size)) {
            fprintf(stderr, "error: write syscall injection failed\n");
            goto EXIT;
        }
        if ((intptr_t)result <= 0) {
            fprintf(stderr, "error: subject write() failed: %s\n", syscall_strerror(result));
            goto EXIT;
        }

        bytes_written += (size_t)result;
    }

    // Close the fd in the subject's memory
    if (!subject_inject_syscall1(subject, &result, SYSCALL_CLOSE, subject_fd)) {
        fprintf(stderr, "error: close syscall injection failed\n");
        goto EXIT;
    }
    if ((intptr_t)result < 0) {
        fprintf(stderr, "error: subject open() failed: %s\n", syscall_strerror(result));
        goto EXIT;
    }

    // Write the tmp filename back to the subject mmap'd area for dlopen()
    if (!memory_write(memory_fd, tmp_filename, subject_data_region, strlen(tmp_filename) + 1)) {
        goto EXIT;
    }

    uint8_t injected_code[] = {
        0x90, // nop
        0x90, // nop
        0x90, // nop (replaced with push rbp sometimes)
        0xff, 0xd0, // call rax
        0xcc, // int 3
    };

    if ((starting_registers.rsp & 15) != 0) {
        injected_code[2] = 0x55; // push rbp
    }

    if (!memory_write(memory_fd, injected_code, subject_code_region, sizeof(injected_code))) {
        goto EXIT;
    }

    // Set up the registers for the injected code
    struct user_regs_struct modified_registers;
    memcpy(&modified_registers, &starting_registers, sizeof(struct user_regs_struct));
    modified_registers.rip = subject_code_region + 2; // Two NOPs were added, in case the kernel subtracts 2 from RIP
    modified_registers.rax = subject_dlopen_address; // RAX = subject dlopen() address
    modified_registers.rdi = subject_data_region; // RDI = pointer to shared library path string
    modified_registers.rsi = RTLD_LAZY; // RSI = RTLD_LAZY
    if (ptrace(PTRACE_SETREGS, pid, NULL, &modified_registers) == -1) {
        fprintf(stderr, "error: failed to PTRACE_SETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    // Let the injected code run
    if (!subject_continue_until_breakpoint(subject)) {
        fprintf(stderr, "error: failed on injected dlopen()\n");
        goto EXIT;
    }

    // Get register state after running dlopen()
    if (ptrace(PTRACE_GETREGS, pid, NULL, &modified_registers) == -1) {
        fprintf(stderr, "error: failed to PTRACE_GETREGS: %s\n", strerror(errno));
        goto EXIT;
    }

    // Check if dlopen() failed and returned NULL
    if (modified_registers.rax == 0) {
        // Setup to run dlerror
        memcpy(&modified_registers, &starting_registers, sizeof(struct user_regs_struct));
        modified_registers.rip = subject_code_region + 2; // Two NOPs were added, in case the kernel subtracts 2 from RIP
        modified_registers.rax = subject_dlerror_address; // RAX = subject dlerror() address
        if (ptrace(PTRACE_SETREGS, pid, NULL, &modified_registers) == -1) {
            fprintf(stderr, "error: failed to PTRACE_SETREGS: %s\n", strerror(errno));
            goto EXIT;
        }

        // Let the injected code run
        if (!subject_continue_until_breakpoint(subject)) {
            fprintf(stderr, "error: failed on injected dlopen()\n");
            goto EXIT;
        }

        // Get register state after running dlerror()
        if (ptrace(PTRACE_GETREGS, pid, NULL, &modified_registers) == -1) {
            fprintf(stderr, "error: failed to PTRACE_GETREGS: %s\n", strerror(errno));
            goto EXIT;
        }

        char *error_string = memory_read_string(memory_fd, modified_registers.rax);
        fprintf(stderr, "error: subject dlopen() failed: %s\n", error_string);
        free(error_string);
        goto EXIT;
    }

    // Restore starting registers
    if (ptrace(PTRACE_SETREGS, pid, NULL, &starting_registers) == -1) {
        fprintf(stderr, "error: failed to PTRACE_SETREGS: %s\n", strerror(errno));
        goto EXIT;
    }
    registers_saved = false;

    // Free mmap'd region
    if (!subject_inject_syscall2(subject, NULL, SYSCALL_MUNMAP, subject_mmap_region, 8192)) {
        fprintf(stderr, "error: munmap syscall injection failed\n");
        goto EXIT;
    }

    success = true;

  EXIT:
    if (registers_saved) {
        // Attempt to restore starting registers
        ptrace(PTRACE_SETREGS, pid, NULL, &starting_registers);
    }
    if (so_file_contents != NULL) {
        free(so_file_contents);
    }
    subject_detach(subject);
    return success;
}


scan_t *subject_begin_scan(subject_t *subject) {
    if (subject == NULL) {
        return NULL;
    }

    scan_t *scan = malloc(sizeof(scan_t));
    scan->subject = (subject_t *)subject;
    scan->hits = NULL;
    scan->hit_count = 0;
    scan->hit_capacity = 0;

    push_scan(scan);
    return scan;
}


void scan_reset(scan_t *scan) {
    if (scan->hits != NULL) {
        free(scan->hits);
    }
    scan->hits = NULL;
    scan->hit_count = 0;
    scan->hit_capacity = 0;
}


void subject_free(subject_t *subject) {
    if (subject == NULL) {
        return;
    }
    if (subject->attached > 0) {
        subject->attached = 1;
    }
    subject_detach(subject);
    while (subject->scans != NULL) {
        scan_free(subject->scans);
    }
    free(subject);
}


scan_t *scan_fork(scan_t *scan) {
    scan_t *result = malloc(sizeof(scan_t));
    memcpy(result, scan, sizeof(scan_t));
    result->hits = malloc(sizeof(size_t) * scan->hit_count);
    memcpy(result->hits, scan->hits, sizeof(size_t) * scan->hit_count);
    push_scan(result);
    return result;
}


bool scan_refresh(scan_t *scan, gval_type_e type) {
    bool success = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    if (!memory_filter(scan, type, NULL, SEARCH_NOOP)) {
        goto EXIT;
    }

    success = true;

  EXIT:
    subject_detach(subject);
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


bool scan_update(scan_t *scan, gval_type_e type, search_op_e op, ...) {
    int memory_fd = -1;
    bool success = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;

    gval_u value;
    va_list args;
    va_start(args, op);

    switch (type)
    {
        case TYPE_UINT8: value.uint8 = (uint8_t)va_arg(args, unsigned); break;
        case TYPE_UINT16: value.uint16 = (uint16_t)va_arg(args, unsigned); break;
        case TYPE_UINT32: value.uint32 = va_arg(args, uint32_t); break;
        case TYPE_UINT64: value.uint64 = va_arg(args, uint64_t); break;
        case TYPE_INT8: value.int8 = (int8_t)va_arg(args, int); break;
        case TYPE_INT16: value.int16 = (int16_t)va_arg(args, int); break;
        case TYPE_INT32: value.int32 = va_arg(args, int32_t); break;
        case TYPE_INT64: value.int64 = va_arg(args, int64_t); break;
        case TYPE_FLOAT32: value.float32 = (float)va_arg(args, double); break;
        case TYPE_FLOAT64: value.float64 = va_arg(args, double); break;
        case TYPE_BYTES_16: {
            const uint8_t *buffer = va_arg(args, uint8_t *);
            memcpy(value.b16, buffer, 16);
            break;
        };
    }

    va_end(args);

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
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
            if (!region->read || !region->write) {
                continue;
            }
            if (!memory_search(scan, type, region->offset, region->size, &value, op)) {
                free_maps(maps);
                goto EXIT;
            }
        }

        free_maps(maps);
    } else {
        if (!memory_filter(scan, type, &value, op)) {
            goto EXIT;
        }
    }

    success = true;

  EXIT:
    subject_detach(subject);
    return success;
}


bool scan_set_value(scan_t *scan, gval_type_e type, ...) {
    bool success = false;
    subject_t *subject = scan->subject;
    pid_t pid = subject->pid;
    int memory_fd = subject->memory_fd;

    gval_u value;
    va_list args;
    va_start(args, type);

    switch (type)
    {
        case TYPE_UINT8: value.uint8 = (uint8_t)va_arg(args, unsigned); break;
        case TYPE_UINT16: value.uint16 = (uint16_t)va_arg(args, unsigned); break;
        case TYPE_UINT32: value.uint32 = va_arg(args, uint32_t); break;
        case TYPE_UINT64: value.uint64 = va_arg(args, uint64_t); break;
        case TYPE_INT8: value.int8 = (int8_t)va_arg(args, int); break;
        case TYPE_INT16: value.int16 = (int16_t)va_arg(args, int); break;
        case TYPE_INT32: value.int32 = va_arg(args, int32_t); break;
        case TYPE_INT64: value.int64 = va_arg(args, int64_t); break;
        case TYPE_FLOAT32: value.float32 = (float)va_arg(args, double); break;
        case TYPE_FLOAT64: value.float64 = va_arg(args, double); break;
        case TYPE_BYTES_16: {
            const uint8_t *buffer = va_arg(args, uint8_t *);
            memcpy(value.b16, buffer, 16);
            break;
        }
    }

    va_end(args);

    if (!subject_attach(subject)) {
        fprintf(stderr, "error: failed to attach to subject\n");
        return false;
    }

    size_t value_size = gval_size(type);
    for (size_t i=0; i < scan->hit_count; i++) {
        size_t hit = scan->hits[i];
        lseek(memory_fd, hit, SEEK_SET);
        write(memory_fd, &value, value_size);
    }

    success = true;

  EXIT:
    subject_detach(subject);
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
