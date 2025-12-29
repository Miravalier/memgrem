#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

volatile double x = 4623466932.0;


int main(void) {
    printf("PID: %d\n", getpid());
    printf("Waiting for value to change...\n");

    while (fabs(x - 4623466932.0) < 0.00001) {
        sched_yield();
    }

    printf("Value mutated to %f!\n", x);
    return 0;
}
