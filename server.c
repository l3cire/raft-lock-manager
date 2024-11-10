#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "packet_format.h"
#include "server_rpc.h"
#include "tmdspinlock.h"
#include "raft.h"

server_rpc_conn_t rpc;
tmdspinlock_t lock;
raft_state_t raft;
raft_log_entry_t current_log_entry;

void print_transaction() {
    printf("TRANSACTION %i, CLIENT %i\n", current_log_entry.id, current_log_entry.client);
    for(int i = 0; i < MAX_TRANSACTION_ENTRIES; ++i) {
	if(current_log_entry.data[i].filename[0] == 0) break;
	printf("FILE: '%s': '%s'\n", current_log_entry.data[i].filename, current_log_entry.data[i].buffer);
    }
    printf("\n");
}

int handle_lock_acquire(int client_id, char* message) {
    if(tmdspinlock_acquire(&lock, client_id) < 0) {
	strcpy(message, "the client already has the lock\n");
	return E_LOCK;
    }
    bzero(current_log_entry.data, sizeof(raft_transaction_entry_t)*MAX_TRANSACTION_ENTRIES);
    current_log_entry.client = client_id;
    current_log_entry.id++;
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
    
    int result = E_TRANSACTION_LIMIT; 
    for(int i = 0; i < MAX_TRANSACTION_ENTRIES; ++i) {
	raft_transaction_entry_t *entry = &current_log_entry.data[i];
	if(entry->filename[0] == 0) {
	    strcpy(entry->filename, filename);
	    strcpy(entry->buffer, buffer);
	    result = 0;
	    break;
	} else if(strcmp(entry->filename, filename) == 0) {
	    strcat(entry->buffer, buffer);
	    result = 0;
	    break;
	}
    }

    /*
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
    */
    if(result == E_TRANSACTION_LIMIT) {
	strcpy(message, "too many files modified within one transaction");
    } else {
	strcpy(message, "success");
    }
    tmdspinlock_reset_if_owner(&lock, client_id);
    return result;
}

void handle_raft_commit(raft_transaction_entry_t data[MAX_TRANSACTION_ENTRIES]) {

}

void* raft_listener_thread(void* arg) {
    Raft_RPC_listen(&raft);
    pthread_exit(0);
}

int main(int argc, char *argv[]) {
    // initialize rpc handler
    raft_configuration_t config;
    FILE *f = fopen(argv[0], "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    int id = atoi(argv[1]);
    int port_client = atoi(argv[2]);
    int port_raft = atoi(argv[3]);
    printf("starting server %i on port %i (raft port %i)\n", id, port_client, port_raft);
/*
    printf("config for the server is: \n");
    for(int i = 0; i < N_SERVERS; ++i) {
	printf("    server %i, client_port = %i, raft_port = %i\n", config.servers[i].id, ntohs(config.servers[i].client_socket.sin_port), ntohs(config.servers[i].raft_socket.sin_port));
    }
*/
    Raft_server_init(&raft, config, handle_raft_commit, id, port_raft); 
    pthread_t tid;
    pthread_create(&tid, NULL, raft_listener_thread, NULL);

    bzero(&rpc, sizeof(server_rpc_conn_t));
    Server_RPC_init(&rpc, &raft, port_client);

    // set up handlers for the RPCs
    rpc.handle_lock_acquire = handle_lock_acquire;
    rpc.handle_lock_release = handle_lock_release;
    rpc.handle_append_file = handle_append_file;

    // initialize the spinlock
    tmdspinlock_init(&lock);
    // start listening for requests
    Server_RPC_listen(&rpc);
}

