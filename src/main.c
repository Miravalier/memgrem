#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "string_list.h"
#include "subject.h"


typedef enum command_type_e {
    CMD_FIND_EXACT,
    CMD_FIND_APPROXIMATE,
    CMD_FIND_BOUNDED,
    CMD_SET_VALUE,
    CMD_REFRESH,
    CMD_ELIMINATE,
    CMD_QUIT,
    CMD_PRINT,
    CMD_SW_LOCK,
    CMD_SW_UNLOCK,
    CMD_HW_LOCK,
    CMD_HW_UNLOCK,
} command_type_e;

typedef struct command_eliminate_t {
    command_type_e type;
    size_t value;
} command_eliminate_t;

typedef struct command_find_approximate_t {
    command_type_e type;
    double value;
} command_find_approximate_t;

typedef struct command_find_exact_t {
    command_type_e type;
    double value;
} command_find_exact_t;

typedef struct command_set_value_t {
    command_type_e type;
    double value;
} command_set_value_t;

typedef struct command_find_bounded_t {
    command_type_e type;
    double min_value;
    double max_value;
} command_find_bounded_t;

typedef struct command_write_lock_t {
    command_type_e type;
    lock_t value;
} command_write_lock_t;

typedef union command_u {
    command_type_e type;
    command_find_exact_t exact;
    command_set_value_t set;
    command_find_bounded_t bounded;
    command_eliminate_t eliminate;
    command_find_approximate_t approximate;
    command_write_lock_t write_lock;
} command_u;


static void get_input_line(char *buffer, size_t buffer_size) {
    int c;
    size_t bytes_read = 0;
    while ((c = getchar()) != '\n') {
        if (bytes_read < buffer_size) {
            buffer[bytes_read++] = c;
        }
    }
    buffer[bytes_read] = '\0';
}


static bool gval_type_from_str(gval_type_e *type, const char *string) {
    if (streq(string, "f32")) {
        *type = TYPE_FLOAT32;
        return true;
    }

    if (streq(string, "f64")) {
        *type = TYPE_FLOAT64;
        return true;
    }

    return false;
}


bool gval_from_str(gval_type_e type, gval_u *value, const char *string) {
    char *end;
    if (type == TYPE_FLOAT32) {
        value->float32 = strtof(string, &end);
        return *end == '\0';
    } else if (type == TYPE_FLOAT64) {
        value->float64 = strtod(string, &end);
        return *end == '\0';
    }
    return false;
}


static void get_command(command_u *command) {
    string_list_t *args = NULL;
    char line[256];
    while (true) {
        printf("> ");
        get_input_line(line, sizeof(line) - 1);

        if (args != NULL) {
            string_list_free(args);
        }
        args = string_split(line, " ", false);
        if (args->length == 0) {
            continue;
        }

        const char *cmd = args->strings[0];
        char *end;

        double plain_value = strtod(cmd, &end);
        if (*end == '\0') {
            command->type = CMD_FIND_APPROXIMATE;
            command->approximate.value = plain_value;
            break;
        }

        if (streq(cmd, "print")) {
            command->type = CMD_PRINT;
            break;
        }

        if (streq(cmd, "lock")) {
            if (args->length != 4) {
                printf("usage: lock <addr> <type> <value>\n");
                continue;
            }
            command->type = CMD_SW_LOCK;
            command->write_lock.value.location = strtoul(args->strings[1], &end, 16);
            if (*end != '\0') {
                printf("error: invalid write lock address\n");
                continue;
            }
            if (!gval_type_from_str(&command->write_lock.value.type, args->strings[2])) {
                printf("error: invalid write lock type\n");
                continue;
            }
            if (!gval_from_str(command->write_lock.value.type, &command->write_lock.value.value, args->strings[3])) {
                printf("error: invalid write lock value\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "unlock")) {
            if (args->length != 2) {
                printf("usage: unlock <addr|all>\n");
                continue;
            }
            command->type = CMD_SW_UNLOCK;
            if (streq(args->strings[1], "all")) {
                command->write_lock.value.location = 0;
            } else {
                command->write_lock.value.location = strtoul(args->strings[1], &end, 16);
                if (*end != '\0') {
                    printf("error: invalid address\n");
                    continue;
                }
            }
            break;
        }

        if (streq(cmd, "quit") || streq(cmd, "q")) {
            command->type = CMD_QUIT;
            break;
        }

        if (streq(cmd, "eliminate") || streq(cmd, "e")) {
            if (args->length != 2) {
                printf("usage: = <value>\n");
                continue;
            }
            command->type = CMD_ELIMINATE;
            command->eliminate.value = strtoul(args->strings[1], &end, 10);
            if (*end != '\0') {
                printf("error: invalid float64 value\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "exact")) {
            if (args->length != 2) {
                printf("usage: = <value>\n");
                continue;
            }
            command->type = CMD_FIND_EXACT;
            command->exact.value = strtod(args->strings[1], &end);
            if (*end != '\0') {
                printf("error: invalid float64 value\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "approximate")) {
            if (args->length != 2) {
                printf("usage: ~ <value>\n");
                continue;
            }
            command->type = CMD_FIND_APPROXIMATE;
            command->approximate.value = strtod(args->strings[1], &end);
            if (*end != '\0') {
                printf("error: invalid float64 value\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "set")) {
            if (args->length != 2) {
                printf("usage: set <value>\n");
                continue;
            }
            command->type = CMD_SET_VALUE;
            command->set.value = strtod(args->strings[1], &end);
            if (*end != '\0') {
                printf("error: invalid float64 value\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "bounded")) {
            if (args->length != 3) {
                printf("usage: set <min> <max>\n");
                continue;
            }
            command->type = CMD_FIND_BOUNDED;
            command->bounded.min_value = strtod(args->strings[1], &end);
            if (*end != '\0') {
                printf("error: invalid float64 min\n");
                continue;
            }
            command->bounded.max_value = strtod(args->strings[2], &end);
            if (*end != '\0') {
                printf("error: invalid float64 max\n");
                continue;
            }
            break;
        }

        if (streq(cmd, "refresh")) {
            command->type = CMD_REFRESH;
            break;
        }

        printf("error: unrecognized command '%s'\n", cmd);
    }

    if (args != NULL) {
        string_list_free(args);
    }
}


int main(int argc, char **argv) {
    // Handle cmd line arguments
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s <pid> [all|float|int|f32|f64|i32|i64]\n", argv[0]);
        return 1;
    }

    char *end;
    unsigned long pid_arg = strtoul(argv[1], &end, 10);
    if (*end != '\0') {
        fprintf(stderr, "error: invalid pid '%s' not a number\n", argv[1]);
        return 1;
    }

    if (pid_arg == 0 || pid_arg > INT_MAX) {
        fprintf(stderr, "error: pid %lu out of range (%d-%d)\n", pid_arg, 1, INT_MAX);
        return 1;
    }

    const char *mode;
    if (argc == 2) {
        mode = "all";
    } else {
        mode = argv[2];
    }

    // Create subject and inject shared object
    pid_t pid = (pid_t)pid_arg;
    subject_t *subject = subject_create(pid);
    if (subject == NULL) {
        fprintf(stderr, "error: failed to attach to pid %d\n", pid);
        return 1;
    }

    // Start scans
    scan_t *float32_scan = NULL;
    scan_t *float64_scan = NULL;
    scan_t *int32_scan = NULL;
    scan_t *int64_scan = NULL;
    size_t scan_count = 0;

    if (streq(mode, "all") || streq(mode, "float") || streq(mode, "f32")) {
        float32_scan = subject_begin_scan(subject);
        scan_count++;
    }

    if (streq(mode, "all") || streq(mode, "float") || streq(mode, "f64")) {
        float64_scan = subject_begin_scan(subject);
        scan_count++;
    }

    if (streq(mode, "all") || streq(mode, "int") || streq(mode, "i32")) {
        int32_scan = subject_begin_scan(subject);
        scan_count++;
    }

    if (streq(mode, "all") || streq(mode, "int") || streq(mode, "i64")) {
        int64_scan = subject_begin_scan(subject);
        scan_count++;
    }

    if (scan_count == 0) {
        fprintf(stderr, "error: invalid mode '%s'\n", mode);
        fprintf(stderr, "usage: %s <pid> [all|float|f32|f64]\n", argv[0]);
    }

    if (scan_count == 1) {
        printf("1 scan created\n");
    } else {
        printf("%zu scans created\n", scan_count);
    }

    // Handle commands
    while (true) {
        subject_detach(subject);
        command_u command;
        get_command(&command);
        if (!subject_attach(subject)) {
            break;
        }

        bool scans_changed = false;

        if (command.type == CMD_QUIT) {
            break;
        }

        else if (command.type == CMD_SW_LOCK) {
            if (!subject_command_sw_lock(subject, &command.write_lock.value)) {
                printf("error: failed to issue wlock command\n");
                break;
            }
        }

        else if (command.type == CMD_SW_UNLOCK) {
            if (!subject_command_sw_unlock(subject, command.write_lock.value.location)) {
                printf("error: failed to issue wlock command\n");
                break;
            }
        }

        else if (command.type == CMD_PRINT) {
            if (!subject_command_print(subject, NULL)) {
                printf("error: failed to issue print command\n");
                break;
            }
        }

        else if (command.type == CMD_FIND_BOUNDED) {
            scans_changed = true;
            if (float32_scan) {
                if (!scan_update(float32_scan, TYPE_FLOAT32, SEARCH_GREATER, (float)command.bounded.min_value)) {
                    printf("error: failed to float32 SEARCH_GREATER\n");
                    break;
                }
                if (!scan_update(float32_scan, TYPE_FLOAT32, SEARCH_LESS, (float)command.bounded.max_value)) {
                    printf("error: failed to float32 SEARCH_LESS\n");
                    break;
                }
            }
            if (float64_scan) {
                if (!scan_update(float64_scan, TYPE_FLOAT64, SEARCH_GREATER, command.bounded.min_value)) {
                    printf("error: failed to float64 SEARCH_GREATER\n");
                    break;
                }
                if (!scan_update(float64_scan, TYPE_FLOAT64, SEARCH_LESS, command.bounded.max_value)) {
                    printf("error: failed to float64 SEARCH_LESS\n");
                    break;
                }
            }
            if (int32_scan) {
                if (!scan_update(int32_scan, TYPE_INT32, SEARCH_GREATER, (int32_t)command.bounded.min_value)) {
                    printf("error: failed to int32 SEARCH_GREATER\n");
                    break;
                }
                if (!scan_update(int32_scan, TYPE_INT32, SEARCH_LESS, (int32_t)command.bounded.max_value)) {
                    printf("error: failed to int32 SEARCH_LESS\n");
                    break;
                }
            }
            if (int64_scan) {
                if (!scan_update(int64_scan, TYPE_INT64, SEARCH_GREATER, (int64_t)command.bounded.min_value)) {
                    printf("error: failed to int64 SEARCH_GREATER\n");
                    break;
                }
                if (!scan_update(int64_scan, TYPE_INT64, SEARCH_LESS, (int64_t)command.bounded.max_value)) {
                    printf("error: failed to int64 SEARCH_LESS\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_FIND_EXACT) {
            scans_changed = true;
            if (float32_scan) {
                if (!scan_update(float32_scan, TYPE_FLOAT32, SEARCH_EQUAL, (float)command.exact.value)) {
                    printf("error: failed to float32 SEARCH_EQUAL\n");
                    break;
                }
            }
            if (float64_scan) {
                if (!scan_update(float64_scan, TYPE_FLOAT64, SEARCH_EQUAL, command.exact.value)) {
                    printf("error: failed to float64 SEARCH_EQUAL\n");
                    break;
                }
            }
            if (int32_scan) {
                if (!scan_update(int32_scan, TYPE_INT32, SEARCH_EQUAL, (int32_t)command.exact.value)) {
                    printf("error: failed to int32 SEARCH_EQUAL\n");
                    break;
                }
            }
            if (int64_scan) {
                if (!scan_update(int64_scan, TYPE_INT64, SEARCH_EQUAL, (int64_t)command.exact.value)) {
                    printf("error: failed to int64 SEARCH_EQUAL\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_FIND_APPROXIMATE) {
            scans_changed = true;
            if (float32_scan) {
                if (!scan_update(float32_scan, TYPE_FLOAT32, SEARCH_APPROX, (float)command.exact.value)) {
                    printf("error: failed to float32 SEARCH_APPROX\n");
                    break;
                }
            }
            if (float64_scan) {
                if (!scan_update(float64_scan, TYPE_FLOAT64, SEARCH_APPROX, command.exact.value)) {
                    printf("error: failed to float64 SEARCH_APPROX\n");
                    break;
                }
            }
            if (int32_scan) {
                if (!scan_update(int32_scan, TYPE_INT32, SEARCH_APPROX, (int32_t)command.exact.value)) {
                    printf("error: failed to int32 SEARCH_APPROX\n");
                    break;
                }
            }
            if (int64_scan) {
                if (!scan_update(int64_scan, TYPE_INT64, SEARCH_APPROX, (int64_t)command.exact.value)) {
                    printf("error: failed to int64 SEARCH_APPROX\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_SET_VALUE) {
            scans_changed = true;
            if (float32_scan) {
                if (!scan_set_value(float32_scan, TYPE_FLOAT32, (float)command.set.value)) {
                    printf("error: failed to float32 SET_VALUE\n");
                    break;
                }
                scan_refresh(float32_scan, TYPE_FLOAT32);
            }
            if (float64_scan) {
                if (!scan_set_value(float64_scan, TYPE_FLOAT64, command.set.value)) {
                    printf("error: failed to float64 SET_VALUE\n");
                    break;
                }
                scan_refresh(float64_scan, TYPE_FLOAT64);
            }
            if (int32_scan) {
                if (!scan_set_value(int32_scan, TYPE_INT32, (int32_t)command.set.value)) {
                    printf("error: failed to int32 SET_VALUE\n");
                    break;
                }
                scan_refresh(int32_scan, TYPE_INT32);
            }
            if (int64_scan) {
                if (!scan_set_value(int64_scan, TYPE_INT64, (int64_t)command.set.value)) {
                    printf("error: failed to int64 SET_VALUE\n");
                    break;
                }
                scan_refresh(int64_scan, TYPE_INT64);
            }
        }

        else if (command.type == CMD_REFRESH) {
            scans_changed = true;
            if (float32_scan) {
                scan_refresh(float32_scan, TYPE_FLOAT32);
            }
            if (float64_scan) {
                scan_refresh(float64_scan, TYPE_FLOAT64);
            }
            if (int32_scan) {
                scan_refresh(int32_scan, TYPE_INT32);
            }
            if (int64_scan) {
                scan_refresh(int64_scan, TYPE_INT64);
            }
        }

        else if (command.type == CMD_ELIMINATE) {
            scans_changed = true;
            bool eliminate_match = false;
            size_t target_index = command.eliminate.value;
            if (float32_scan && !eliminate_match) {
                if (target_index < float32_scan->hit_count) {
                    scan_eliminate(float32_scan, target_index);
                    eliminate_match = true;
                } else {
                    target_index -= float32_scan->hit_count;
                }
            }
            if (float64_scan && !eliminate_match) {
                if (target_index < float64_scan->hit_count) {
                    scan_eliminate(float64_scan, target_index);
                    eliminate_match = true;
                } else {
                    target_index -= float64_scan->hit_count;
                }
            }
            if (int32_scan && !eliminate_match) {
                if (target_index < int32_scan->hit_count) {
                    scan_eliminate(int32_scan, target_index);
                    eliminate_match = true;
                } else {
                    target_index -= int32_scan->hit_count;
                }
            }
            if (int64_scan && !eliminate_match) {
                if (target_index < int64_scan->hit_count) {
                    scan_eliminate(int64_scan, target_index);
                    eliminate_match = true;
                } else {
                    target_index -= int64_scan->hit_count;
                }
            }
            if (!eliminate_match) {
                printf("error: invalid index number\n");
                continue;
            }
        }

        if (scans_changed) {
            size_t total_hit_count = 0;
            if (float32_scan) {
                total_hit_count += float32_scan->hit_count;
            }
            if (float64_scan) {
                total_hit_count += float64_scan->hit_count;
            }
            if (int32_scan) {
                total_hit_count += int32_scan->hit_count;
            }
            if (int64_scan) {
                total_hit_count += int64_scan->hit_count;
            }

            printf("Matches: %zu\n", total_hit_count);

            size_t section_limit = 48 / scan_count;
            size_t hit_index = 0;
            if (float32_scan) {
                for (size_t i=0; i < section_limit && i < float32_scan->hit_count; i++) {
                    gval_u value = float32_scan->values[i];
                    printf("%zu. %f 0x%zx (Float32)\n", hit_index+i, value.float32, float32_scan->hits[i]);
                }
                if (float32_scan->hit_count >= section_limit) {
                    printf("...\n");
                }
                hit_index += float32_scan->hit_count;
            }
            if (float64_scan) {
                for (size_t i=0; i < section_limit && i < float64_scan->hit_count; i++) {
                    gval_u value = float64_scan->values[i];
                    printf("%zu. %lf 0x%zx (Float64)\n", hit_index+i, value.float64, float64_scan->hits[i]);
                }
                if (float64_scan->hit_count >= section_limit) {
                    printf("...\n");
                }
                hit_index += float64_scan->hit_count;
            }
            if (int32_scan) {
                for (size_t i=0; i < section_limit && i < int32_scan->hit_count; i++) {
                    gval_u value = int32_scan->values[i];
                    printf("%zu. %d 0x%zx (Int32)\n", hit_index+i, value.int32, int32_scan->hits[i]);
                }
                if (int32_scan->hit_count >= section_limit) {
                    printf("...\n");
                }
                hit_index += int32_scan->hit_count;
            }
            if (int64_scan) {
                for (size_t i=0; i < section_limit && i < int64_scan->hit_count; i++) {
                    gval_u value = int64_scan->values[i];
                    printf("%zu. %ld 0x%zx (Int64)\n", hit_index+i, value.int64, int64_scan->hits[i]);
                }
                if (int64_scan->hit_count >= section_limit) {
                    printf("...\n");
                }
                hit_index += int64_scan->hit_count;
            }
        }
    }

    subject_free(subject);
    return 0;
}
