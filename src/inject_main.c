#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "inject_control.h"


__attribute__((constructor))
void inject_main(void)
{
    // Create a thread
    // In that thread, wait for the canaries to die
    // Wait for instructions to appear in the control buffer
}
