#ifndef __SPINLOCK_h__
#define __SPINLOCK_h__

typedef struct spinlock {
    int lock_flag;
} spinlock_t;

int spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock); 
void spinlock_release(spinlock_t *lock);

#endif
