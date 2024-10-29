#include "timer.h"
#include "spinlock.h"
#include <pthread.h>
#include <time.h>
#include <signal.h>

void* timer_thread(void* arg) {
    timer_t *timer = (timer_t*) arg;
    while(1) {
	spinlock_acquire(&timer->lock);
	if(timer->active) {
	    clock_t time_diff = clock() - timer->start_time;
	    int time_diff_msec = time_diff * 1000 / CLOCKS_PER_SEC;
	    if(time_diff_msec >= timer->duration) {
		timer->active = 0;
		spinlock_release(&timer->lock);
		timer->handle_timer();
	    } else {
		spinlock_release(&timer->lock);
	    }
	} else {
	    spinlock_release(&timer->lock);
	}
	sched_yield();
    }
    pthread_exit(0);
}

void timer_init(timer_t *timer, int duration, void (*timer_handler)()) {
    timer->duration = duration;
    timer->active = 0;
    spinlock_init(&timer->lock);
    timer->handle_timer = timer_handler;
    pthread_create(&timer->thread_id, NULL, timer_thread, timer);
}

void timer_reset(timer_t *timer) {
    spinlock_acquire(&timer->lock);
    timer->start_time = clock();
    timer->active = 1;
    spinlock_release(&timer->lock);
}

void timer_disable(timer_t *timer) {
    spinlock_acquire(&timer->lock);
    timer->active = 0;
    spinlock_release(&timer->lock);
}

void timer_terminate(timer_t *timer) {
    pthread_kill(timer->thread_id, SIGKILL);
    timer->active = 0;
}

