#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "server_rpc.h"
#include "tmdspinlock.h"

tmdspinlock_t lock;

int handle_lock_acquire(int client_id, char* message) {
    if(tmdspinlock_acquire(&lock, client_id) < 0) {
	strcpy(message, "the client already has the lock\n");
	return E_LOCK;
    }
    strcpy(message, "lock acquired");
    return 0;
}

int handle_lock_release(int client_id, char* message) {
    if(tmdspinlock_release(&lock, client_id) < 0) {
	strcpy(message, "lock released before being acquired");
	return E_LOCK_EXP;
    }
    strcpy(message, "lock released");
    return 0;
}

int handle_append_file(int client_id, char* filename, char* buffer, char* message) {
    if(tmdspinlock_pause_if_owner(&lock, client_id) < 0) {
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
    tmdspinlock_reset_if_owner(&lock, client_id);
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
    tmdspinlock_init(&lock);
    // start listening for requests
    Server_RPC_listen(&rpc);
}

