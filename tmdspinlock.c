#include "tmdspinlock.h"

int tmdspinlock_pause_if_owner(tmdspinlock_t *lock, int id) {
    // if we don't hold a lock, immediately return -1;
    if(lock->holder_id != id) {
	return -1;
    }
    // disabling the timer
    timer_disable(&lock->timer);
    // while we were waiting for the timer to disable, 
    // the timer could've triggered and released our lock -- so need to check once again
    if(lock->holder_id != id) {
	timer_resume(&lock->timer); // if lock was released, continue timer
	return -1;
    }
    return 0;
}

int tmdspinlock_reset_if_owner(tmdspinlock_t *lock, int id) {
    if(lock->holder_id != id) {
	return -1;
    }
    timer_reset(&lock->timer);
    return 0;
}

int tmdspinlock_acquire(tmdspinlock_t *lock, int id) {
    if(lock->holder_id == id) {
	return E_LOCK;
    }

    while(!__sync_bool_compare_and_swap(&lock->lock_flag, 0, 1));
    lock->holder_id = id;
    timer_reset(&lock->timer);
    return 0;
}

int tmdspinlock_release(tmdspinlock_t *lock, int id) {
    if(tmdspinlock_pause_if_owner(lock, id) != 0) {
	return E_LOCK_EXP;
    }
    lock->holder_id = -1;

    __sync_bool_compare_and_swap(&lock->lock_flag, 1, 0);
    return 0;
}

void _tmdspinlock_handle_timer(void *arg) {
    tmdspinlock_t *lock = (tmdspinlock_t*)arg;
    assert(lock->holder_id != -1);
    lock->holder_id = -1;

    __sync_bool_compare_and_swap(&lock->lock_flag, 1, 0);
}

void tmdspinlock_init(tmdspinlock_t *lock) {
    lock->lock_flag = 0;
    lock->holder_id = -1;
    timer_init(&lock->timer, CLIENT_TIMEOUT, _tmdspinlock_handle_timer, lock);
}

void tmdspinlock_terminate(tmdspinlock_t *lock)  {
    timer_disable(&lock->timer);
    timer_terminate(&lock->timer);
}

