#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "string_list.h"
#include "subject.h"


typedef enum command_type_e {
    CMD_FIND_EXACT,
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
} command_u;


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

        if (streq(cmd, "quit") || streq(cmd, "q")) {
            command->type = CMD_QUIT;
            break;
        }

        if (streq(cmd, "exact") || streq(cmd, "e") || streq(cmd, "=")) {
            if (args->length != 2) {
                printf("usage: exact <value>\n");
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

        if (streq(cmd, "about") || streq(cmd, "a") || streq(cmd, "~")) {
            if (args->length != 2) {
                printf("usage: about <value>\n");
                continue;
            }
            command->type = CMD_FIND_BOUNDED;
            double value = strtod(args->strings[1], &end);
            if (*end != '\0') {
                printf("error: invalid float64 value\n");
                continue;
            }
            command->bounded.min_value = value - 1.0;
            command->bounded.max_value = value + 1.0;
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
    if (argc != 2) {
        fprintf(stderr, "error: expected 1 argument, received %d\n", argc - 1);
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

    scan_t *float32_scan = subject_begin_scan(subject, SCANTYPE_FLOAT32);
    scan_t *float64_scan = subject_begin_scan(subject, SCANTYPE_FLOAT64);
    printf("Scans created.\n");

    while (true) {
        command_u command;
        get_command(&command);

        if (command.type == CMD_QUIT) {
            break;
        } else if (command.type == CMD_FIND_BOUNDED) {
            if (!scan_update(float32_scan, SEARCH_GREATER, (float)command.bounded.min_value)) {
                printf("error: failed to float32 SEARCH_GREATER\n");
                break;
            }
            sleep(1);
            if (!scan_update(float32_scan, SEARCH_LESS, (float)command.bounded.max_value)) {
                printf("error: failed to float32 SEARCH_LESS\n");
                break;
            }
            sleep(1);
            if (!scan_update(float64_scan, SEARCH_GREATER, command.bounded.min_value)) {
                printf("error: failed to float64 SEARCH_GREATER\n");
                break;
            }
            sleep(1);
            if (!scan_update(float64_scan, SEARCH_LESS, command.bounded.max_value)) {
                printf("error: failed to float64 SEARCH_LESS\n");
                break;
            }
        } else if (command.type == CMD_FIND_EXACT) {
            if (!scan_update(float32_scan, SEARCH_EQUAL, (float)command.exact.value)) {
                printf("error: failed to float32 SEARCH_EQUAL\n");
                break;
            }
            sleep(1);
            if (!scan_update(float64_scan, SEARCH_EQUAL, command.exact.value)) {
                printf("error: failed to float64 SEARCH_EQUAL\n");
                break;
            }
        } else if (command.type == CMD_SET_VALUE) {
            if (!scan_set_value(float32_scan, (float)command.set.value)) {
                printf("error: failed to float32 SET_VALUE\n");
                break;
            }
            sleep(1);
            if (!scan_set_value(float64_scan, command.set.value)) {
                printf("error: failed to float64 SET_VALUE\n");
                break;
            }
        } else if (command.type == CMD_REFRESH) {
            scan_refresh(float32_scan);
            sleep(1);
            scan_refresh(float64_scan);
        } else if (command.type == CMD_ELIMINATE) {
            scan_t *target_scan;
            size_t target_index;
            if (command.eliminate.value <= float32_scan->hit_count) {
                target_scan = float32_scan;
                target_index = command.eliminate.value - 1;
            } else {
                target_scan = float64_scan;
                target_index = command.eliminate.value - (float32_scan->hit_count + 1);
            }
            scan_eliminate(target_scan, target_index);
        }

        size_t values_printed = 0;
        for (size_t i=0; i < 32 && i < float32_scan->hit_count; i++) {
            scan_value_u value = float32_scan->values[i];
            printf("%zu. %f\n", ++values_printed, value.float32);
        }

        for (size_t i=0; i < 32 && i < float64_scan->hit_count; i++) {
            scan_value_u value = float64_scan->values[i];
            printf("%zu. %lf\n", ++values_printed, value.float64);
        }
    }

    subject_free(subject);
    return 0;
}
