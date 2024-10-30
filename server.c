#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "server_rpc.h"
#include "spinlock.h"
#include "timer.h"

spinlock_t lock;
atomic_int lock_holder;
timer_t timer;

#define CLIENT_TIMEOUT 1000


// check_lock_pause(client_id)
// if client_id holds a lock, pauses the timer and returns 0
// otherwise, returns -1.
// it is guaranteed that the timer will never trigger after check_lock_pause returns 0,
// unless explicitly continued
int check_lock_pause(int client_id) {
    // if we don't hold a lock, immediately return -1;
    if(lock_holder != client_id) {
	return -1;
    }
    // disabling the timer
    timer_disable(&timer);
    // while we were waiting for the timer to disable, 
    // the timer could've triggered and released our lock -- so need to check once again
    if(lock_holder != client_id) {
	timer_resume(&timer); // if lock was released, continue timer
	return -1;
    }
    return 0;
}

int handle_lock_acquire(int client_id, char* message) {
    if(lock_holder == client_id) {
	strcpy(message, "lock already acquired");
	return E_LOCK;
    }
    spinlock_acquire(&lock);
    lock_holder = client_id;
    strcpy(message, "lock acquired");
    timer_reset(&timer);
    return 0;
}

int handle_lock_release(int client_id, char* message) {
    if(check_lock_pause(client_id) != 0) {
	strcpy(message, "lock released before being acquired");
	return E_LOCK_EXP;
    }
    lock_holder = -1;
    spinlock_release(&lock);
    strcpy(message, "lock released");
    return 0;
}

int handle_append_file(int client_id, char* filename, char* buffer, char* message) {
    if(check_lock_pause(client_id) != 0) {
	strcpy(message, "trying to write to file without holding a lock");
	return E_LOCK_EXP;
    }
    char fn[25] = "./server_files/";
    strcat(fn, filename);
    FILE *f = fopen(fn, "a");
    if(!f) {
	strcpy(message, "file cannot be opened");
	return E_FILE;
    }
    fprintf(f, "%s", buffer);
    fclose(f);
    strcpy(message, "success");
    timer_reset(&timer);
    return 0;
}

void handle_timer(void* arg) {
    assert(lock_holder != -1);
    printf("timer triggered");
    lock_holder = -1;
    spinlock_release(&lock);
}

int main(int argc, char *argv[]) {
    // initialize rpc handler
    server_rpc_conn_t rpc;
    bzero(&rpc, sizeof(server_rpc_conn_t));
    Server_RPC_init(&rpc, 10000);

    // set up handlers for the RPCs
    rpc.handle_lock_acquire = handle_lock_acquire;
    rpc.handle_lock_release = handle_lock_release;
    rpc.handle_append_file = handle_append_file;

    // initialize the spinlock
    spinlock_init(&lock);
    lock_holder = -1;
    timer_init(&timer, CLIENT_TIMEOUT, handle_timer, NULL);

    // start listening for requests
    Server_RPC_listen(&rpc);
}

