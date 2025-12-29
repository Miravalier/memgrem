#include <sched.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>


__attribute__((constructor))
void inject_main(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        while (true) {
            sched_yield();
        }
    }
}
