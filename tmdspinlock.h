#ifndef __SERVER_FILE_MANAGER_h__
#define __SERVER_FILE_MANAGER_h__

#include "server_rpc.h"
#include "spinlock.h"
#include "timer.h"
#include <stdatomic.h>

#define CLIENT_TIMEOUT 1000

// timed spinlock
// this is a version of the spinlock that tracks the current holder ID,
// and releases the lock after a certain timeout wtthout updates from the holding ID.
//
// main invariants:
//		1. holder_id is modified only by the thread that is currently holding the lock and by the timer thread
//			however, holder_id can be read by any process in any time. 
//		2. only the thread with the certain corr. ID can set holder_id to its own ID value
//		3. when the lock is not held, timer is always disabled -- no notifications called when the lock is free
typedef struct tmdspinlock {
	int lock_flag;
	atomic_int holder_id;
	timer_t timer;
} tmdspinlock_t;

// pause_if_owner(id)
// this function checks whether the specified id holds the lock
// if the id holds the lock, disables the timer and returns 0,
// otherwise returns -1 without disabling the timer
//
// if returns 0, it is guaranteed that the id holds a lock, and the lock
// will not be withdrawn by the timer until the reset_if_owner function is called for this id
//
// the main idea behind this function is that when we want to access the resource protected by the lock,
// we don't want the timer to wthdraw the lock from us during the execution -- so when we access the resource,
// we first need to call pause_if_owner, and after we complete the request we call reset_if_owner
int tmdspinlock_pause_if_owner(tmdspinlock_t *lock, int id);

// reset_if_owner(id)
// this function checks whether the specified id holds the lock
// if the id holds the lock, resets the timer to start counting from 0 and returns 0
// otherwise, returns -1
//
// must be called only after pause_if_owner returned 0 for the client!!!
int tmdspinlock_reset_if_owner(tmdspinlock_t *lock, int id);


// acquire(id)
// this function acquires the lock for the specified id and resets the timer 
// to start from 0 after the lock is acquired
int tmdspinlock_acquire(tmdspinlock_t *lock, int id);

// release(id)
// this function releases the lock for the given id if it was not already released by the timer
int tmdspinlock_release(tmdspinlock_t *lock, int id);

// init()
// this function initializes the new tmdspinlock object.
// it starts a new thread for the timer and initializes the lock
void tmdspinlock_init(tmdspinlock_t *lock);

// terminate()
// this function terminates the timer thread.
void tmdspinlock_terminate(tmdspinlock_t *lock);

#endif
