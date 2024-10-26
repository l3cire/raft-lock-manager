#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "server_rpc.h"
#include "spinlock.h"

spinlock_t lock;

int handle_lock_acquire(int client_id, char* message) {
    spinlock_acquire(&lock);
    strcpy(message, "lock acquired");
    return 0;
}

int handle_lock_release(int client_id, char* message) {
    spinlock_release(&lock);
    strcpy(message, "lock released");
    return 0;
}

int handle_append_file(int client_id, char* filename, char* buffer, char* message) {
    char fn[25] = "./server_files/";
    strcat(fn, filename);
    FILE *f = fopen(fn, "a");
    if(!f) {
	strcpy(message, "file cannot be opened");
	return -1;
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

    // start listening for requests
    Server_RPC_listen(&rpc);
}

