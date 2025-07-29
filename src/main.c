#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "subject.h"


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

    printf("[!] Creating subject\n");
    subject_t *subject = subject_create(pid);
    if (subject == NULL) {
        return 1;
    }

    // Wide scan, all 0's
    {
        printf("[!] Beginning scan\n");
        scan_t *scan = subject_begin_scan(subject, SCANTYPE_INT32);
        if (scan == NULL) {
            return 1;
        }

        printf("[!] Updating scan\n");
        if (!scan_update(scan, (int32_t)0)) {
            printf("[!] Scan failed!\n");
            return 1;
        }

        scan_print(scan);

        printf("[!] Updating scan\n");
        if (!scan_update(scan, (int32_t)1)) {
            printf("[!] Scan failed!\n");
            return 1;
        }

        scan_print(scan);
    }

    // Narrow scan, find value and change it
    {
        printf("[!] Beginning scan\n");
        scan_t *scan = subject_begin_scan(subject, SCANTYPE_INT32);
        if (scan == NULL) {
            return 1;
        }

        printf("[!] Updating scan\n");
        if (!scan_update(scan, (int32_t)0x462dc346)) {
            printf("[!] Scan failed!\n");
            return 1;
        }

        scan_print(scan);

        if (!scan_set_value(scan, (int32_t)0x1000)) {
            printf("[!] Set value failed!\n");
            return 1;
        }
    }

    subject_free(subject);

    return 0;
}
