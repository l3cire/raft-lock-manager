#include "../tmdspinlock.h"
#include <pthread.h>


tmdspinlock_t lock;

void* thread_nolock(void* arg) {
    int id = *(int*)arg;
    
    for(int i = 0; i < 5; ++i) {
	printf("thread: %i\n", id);
	usleep(10000);
    }

    pthread_exit(0);
}

void* thread_lock(void* arg) {
    int id = *(int*)arg;
    tmdspinlock_acquire(&lock, id);
    for(int i = 0; i < 5; ++i) {
	printf("thread: %i\n", id);
	usleep(10000);
    }
    tmdspinlock_release(&lock, id);

    pthread_exit(0);
}

void* thread_lock_timer(void* arg) {
    int id = *(int*)arg;
    tmdspinlock_acquire(&lock, id);
    for(int i = 0; i < 5; ++i) {
	if(lock.holder_id == id) printf("thread: %i\n", id);
	usleep(100000);
    }
    tmdspinlock_release(&lock, id);

    pthread_exit(0);
}

void* thread_lock_timer_reacquire(void* arg) {
    int id = *(int*)arg;
    tmdspinlock_acquire(&lock, id);
    for(int i = 0; i < 5; ++i) {
	if(lock.holder_id == id) printf("thread: %i\n", id);
	else tmdspinlock_acquire(&lock, id);
	usleep(100000);
    }
    tmdspinlock_release(&lock, id);

    pthread_exit(0);
}

void* thread_lock_timer_reset(void* arg) {
    int id = *(int*)arg;
    tmdspinlock_acquire(&lock, id);
    for(int i = 0; i < 5; ++i) {
	if(tmdspinlock_pause_if_owner(&lock, id) == 0) {
	    printf("thread: %i\n", id);
	    assert(tmdspinlock_reset_if_owner(&lock, id) == 0);
	}
	usleep(100000);
    }
    tmdspinlock_release(&lock, id);

    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    tmdspinlock_init(&lock);


    pthread_t tid[4];
    int ids[4];
    for(int i = 0; i < 4; ++i) {
	ids[i] = i;
	pthread_create(&tid[i], NULL, thread_nolock, &ids[i]);
    }

    for(int i = 0; i < 4; ++i) {
	pthread_join(tid[i], NULL);
    }

    printf("\n\n");
    
    for(int i = 0; i < 4; ++i) {
	pthread_create(&tid[i], NULL, thread_lock, &ids[i]);
    }
    for(int i = 0; i < 4; ++i) {
	pthread_join(tid[i], NULL);
    }



    printf("\n\n");
    
    for(int i = 0; i < 4; ++i) {
	pthread_create(&tid[i], NULL, thread_lock_timer, &ids[i]);
    }
    for(int i = 0; i < 4; ++i) {
	pthread_join(tid[i], NULL);
    }


    printf("\n\n");
    
    for(int i = 0; i < 4; ++i) {
	pthread_create(&tid[i], NULL, thread_lock_timer_reacquire, &ids[i]);
    }
    for(int i = 0; i < 4; ++i) {
	pthread_join(tid[i], NULL);
    }


    printf("\n\n");
    
    for(int i = 0; i < 4; ++i) {
	pthread_create(&tid[i], NULL, thread_lock_timer_reset, &ids[i]);
    }
    for(int i = 0; i < 4; ++i) {
	pthread_join(tid[i], NULL);
    }



}
