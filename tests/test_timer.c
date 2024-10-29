#include "../timer.h"
#include <stdio.h>

timer_t timer;
int count;

void handle_timer() {
    printf("timer worked!\n");
    count ++;
    if(count < 10) {
	timer_reset(&timer);
    } else {
	timer_terminate(&timer);
	timer_reset(&timer); // check that timer does not work after it is terminated
    }
}

int main(int argc, char* argv[]) {
    timer_init(&timer, 1000, handle_timer);
    count = 0;
    timer_reset(&timer);

    while(1) {};
}
