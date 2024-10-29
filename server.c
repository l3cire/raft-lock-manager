#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "server_rpc.h"
#include "spinlock.h"

spinlock_t lock;
atomic_int lock_holder;

int handle_lock_acquire(int client_id, char* message) {
    if(lock_holder == client_id) {
	strcpy(message, "lock already acquired");
	return E_LOCK;
    }
    spinlock_acquire(&lock);
    lock_holder = client_id;
    strcpy(message, "lock acquired");
    return 0;
}

int handle_lock_release(int client_id, char* message) {
    if(lock_holder != client_id) {
	strcpy(message, "lock released before being acquired");
	return E_LOCK;
    }
    lock_holder = -1;
    spinlock_release(&lock);
    strcpy(message, "lock released");
    return 0;
}

int handle_append_file(int client_id, char* filename, char* buffer, char* message) {
    if(lock_holder != client_id) {
	strcpy(message, "trying to write to file without holding a lock");
	return E_LOCK;
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
    return 0;
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

    // start listening for requests
    Server_RPC_listen(&rpc);
}

