#define _POSIX_C_SOURCE 199309L
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

typedef union command_u {
    command_type_e type;
    command_find_exact_t exact;
    command_set_value_t set;
    command_find_bounded_t bounded;
    command_eliminate_t eliminate;
    command_find_approximate_t approximate;
} command_u;


static void ms_sleep(long milliseconds)
{
    struct timespec sleep_duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000,
    };
    nanosleep(&sleep_duration, &sleep_duration);
}


static bool streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}


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

        if (streq(cmd, "=")) {
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

        if (streq(cmd, "~")) {
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

        if (streq(cmd, "set") || streq(cmd, "s")) {
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

        if (streq(cmd, "bounded") || streq(cmd, "bound") || streq(cmd, "b")) {
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

        if (streq(cmd, "refresh") || streq(cmd, "r")) {
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
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s <pid> [all|float|f32|f64]\n", argv[0]);
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
    pid_t pid = (pid_t)pid_arg;
    subject_t *subject = subject_create(pid);
    if (subject == NULL) {
        fprintf(stderr, "error: failed to attach to pid %d\n", pid);
        return 1;
    }

    const char *mode;
    if (argc == 2) {
        mode = "all";
    } else {
        mode = argv[2];
    }

    scan_t *float32_scan = NULL;
    scan_t *float64_scan = NULL;
    size_t scan_count = 0;

    if (streq(mode, "all") || streq(mode, "float") || streq(mode, "f32")) {
        float32_scan = subject_begin_scan(subject, SCANTYPE_FLOAT32);
        scan_count++;
    }

    if (streq(mode, "all") || streq(mode, "float") || streq(mode, "f64")) {
        float64_scan = subject_begin_scan(subject, SCANTYPE_FLOAT64);
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

    while (true) {
        command_u command;
        get_command(&command);

        if (command.type == CMD_QUIT) {
            break;
        }

        else if (command.type == CMD_FIND_BOUNDED) {
            if (float32_scan) {
                ms_sleep(100);
                if (!scan_update(float32_scan, SEARCH_GREATER, (float)command.bounded.min_value)) {
                    printf("error: failed to float32 SEARCH_GREATER\n");
                    break;
                }
                ms_sleep(100);
                if (!scan_update(float32_scan, SEARCH_LESS, (float)command.bounded.max_value)) {
                    printf("error: failed to float32 SEARCH_LESS\n");
                    break;
                }
            }
            if (float64_scan) {
                ms_sleep(100);
                if (!scan_update(float64_scan, SEARCH_GREATER, command.bounded.min_value)) {
                    printf("error: failed to float64 SEARCH_GREATER\n");
                    break;
                }
                ms_sleep(100);
                if (!scan_update(float64_scan, SEARCH_LESS, command.bounded.max_value)) {
                    printf("error: failed to float64 SEARCH_LESS\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_FIND_EXACT) {
            if (float32_scan) {
                ms_sleep(100);
                if (!scan_update(float32_scan, SEARCH_EQUAL, (float)command.exact.value)) {
                    printf("error: failed to float32 SEARCH_EQUAL\n");
                    break;
                }
            }
            if (float64_scan) {
                ms_sleep(100);
                if (!scan_update(float64_scan, SEARCH_EQUAL, command.exact.value)) {
                    printf("error: failed to float64 SEARCH_EQUAL\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_FIND_APPROXIMATE) {
            if (float32_scan) {
                ms_sleep(100);
                if (!scan_update(float32_scan, SEARCH_APPROX, (float)command.exact.value)) {
                    printf("error: failed to float32 SEARCH_EQUAL\n");
                    break;
                }
            }
            if (float64_scan) {
                ms_sleep(100);
                if (!scan_update(float64_scan, SEARCH_APPROX, command.exact.value)) {
                    printf("error: failed to float64 SEARCH_EQUAL\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_SET_VALUE) {
            if (float32_scan) {
                ms_sleep(100);
                if (!scan_set_value(float32_scan, (float)command.set.value)) {
                    printf("error: failed to float32 SET_VALUE\n");
                    break;
                }
            }
            if (float64_scan) {
                ms_sleep(100);
                if (!scan_set_value(float64_scan, command.set.value)) {
                    printf("error: failed to float64 SET_VALUE\n");
                    break;
                }
            }
        }

        else if (command.type == CMD_REFRESH) {
            if (float32_scan) {
                ms_sleep(100);
                scan_refresh(float32_scan);
            }
            if (float64_scan) {
                ms_sleep(100);
                scan_refresh(float64_scan);
            }
        }

        else if (command.type == CMD_ELIMINATE) {
            bool eliminate_match = false;
            scan_t *target_scan = NULL;
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
            if (!eliminate_match) {
                printf("error: invalid index number\n");
                continue;
            }
        }

        size_t total_hit_count = 0;
        if (float32_scan) {
            total_hit_count += float32_scan->hit_count;
        }
        if (float64_scan) {
            total_hit_count += float64_scan->hit_count;
        }
        printf("Matches: %zu\n", total_hit_count);

        size_t hit_index = 0;
        if (float32_scan) {
            for (size_t i=0; i < 32 && i < float32_scan->hit_count; i++) {
                scan_value_u value = float32_scan->values[i];
                printf("%zu. %f 0x%zx (Float32)\n", hit_index+i, value.float32, float32_scan->hits[i]);
            }
            hit_index += float32_scan->hit_count;
        }
        if (float64_scan) {
            for (size_t i=0; i < 32 && i < float64_scan->hit_count; i++) {
                scan_value_u value = float64_scan->values[i];
                printf("%zu. %lf 0x%zx (Float64)\n", hit_index+i, value.float64, float64_scan->hits[i]);
            }
            hit_index += float64_scan->hit_count;
        }
    }

    subject_free(subject);
    return 0;
}
