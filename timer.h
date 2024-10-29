#include "spinlock.h"
#include <time.h>
#include <pthread.h>

typedef struct timer {
	spinlock_t lock;
	clock_t start_time;
	int duration;
	int active;
	pthread_t thread_id;
	void (*handle_timer)();
} timer_t;

void timer_init(timer_t *timer, int duration, void (*timer_handler)());

void timer_reset(timer_t *timer);

void timer_disable(timer_t *timer);

void timer_terminate(timer_t *timer);

