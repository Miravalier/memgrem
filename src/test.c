#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

volatile double value = 4623466932.0;


int main(void) {
    printf("PID: %d\n", getpid());

    printf("Value is %f\n", value);
    printf("Waiting for value to change...\n");
    while (fabs(value - 4623466932.0) < 0.00001) {
        sched_yield();
    }
    printf("Value mutated to %f!\n", value);

    // while (1) {
    //     value = 50.0;
    //     printf("%f\n", value);
    //     sleep(1);
    //     value = 100.0;
    //     printf("%f\n", value);
    //     sleep(1);
    // }

    return 0;
}
