#include <time.h>

#include "utils.h"

void ms_sleep(long milliseconds)
{
    struct timespec sleep_duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000,
    };
    nanosleep(&sleep_duration, &sleep_duration);
}
