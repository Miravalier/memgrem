#include <sched.h>
#include <stdio.h>
#include <unistd.h>

volatile int x = 0x462dc346;


int main(void) {
    printf("PID: %d\n", getpid());
    printf("Waiting for value to change...\n");

    while (x == 0x462dc346) {
        sched_yield();
    }

    printf("Value mutated to %d!\n", x);
    return 0;
}
