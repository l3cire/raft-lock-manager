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

char files_dir[128];

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
    int log_data[2] = {raft.current_term, current_log_entry.id};
    memcpy(message, log_data, 2*sizeof(int));
    return 0;
}

int handle_lock_release(int client_id, int transaction_term, int transaction_id, char* message) {
    if(transaction_term == raft.current_term) {
	// if the transaction is from our term, we need to add it to the log ans release the lock
	if(tmdspinlock_pause_if_owner(&lock, client_id) == 0) {
	    Raft_append_entry(&raft, &current_log_entry); 
	    tmdspinlock_reset_if_owner(&lock, client_id);
	}
	if(tmdspinlock_release(&lock, client_id) < 0) {
	    strcpy(message, "lock released before being acquired");
	    return E_LOCK_EXP;
	}
    } else {
	printf("getting a request for release() of a previous term %i for client %i\n", transaction_term, client_id);
    }

    int rc = 0;
    while(rc == 0) {
	rc = Raft_is_entry_committed(&raft, transaction_term, transaction_id);
	sched_yield();
    }

    if(rc == 1) {
	strcpy(message, "lock released");
	return 0;
    } else {
	printf("TRANSACTION LOSS for client %i\n", client_id);
	strcpy(message, "transaction lost");
	return E_LOST;
    }
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

    if(result == E_TRANSACTION_LIMIT) {
	strcpy(message, "too many files modified within one transaction");
    } else {
	strcpy(message, "success");
    }
    tmdspinlock_reset_if_owner(&lock, client_id);
    return result;
}

void handle_raft_commit(raft_transaction_entry_t data[MAX_TRANSACTION_ENTRIES]) {
    for(int i = 0; i < MAX_TRANSACTION_ENTRIES; ++i) {
	char* filename = data[i].filename;
	char* buffer = data[i].buffer;
	if(filename[0] == 0) break;

	char fn[256];
	strcpy(fn, files_dir);
	strcat(fn, filename);
	FILE *f = fopen(fn, "a");
	if(!f) continue;
	fprintf(f, "%s", buffer);
	fflush(f);
	fclose(f);
    }
}

void* raft_listener_thread(void* arg) {
    Raft_RPC_listen(&raft);
    pthread_exit(0);
}

int main(int argc, char *argv[]) {
    // initialize rpc handler
    
    raft_configuration_t config;
    FILE *f = fopen(argv[1], "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    int id = atoi(argv[2]);
    int use_backup = 0;
    if(argc >= 4 && strcmp(argv[3], "use-backup") == 0) {
	use_backup = 1;
    }
    int port_client;
    int port_raft;
    for(int i = 0; i < N_SERVERS; ++i) {
	if(config.servers[i].id == id) {
	    port_client = ntohs(config.servers[i].client_socket.sin_port);
	    port_raft = ntohs(config.servers[i].raft_socket.sin_port);
	    strcpy(files_dir, config.servers[i].file_directory);
	}
    }
    printf("starting server %i on port %i (raft port %i) -- saving files to %s\n", id, port_client, port_raft, files_dir);
/*
    printf("config for the server is: \n");
    for(int i = 0; i < N_SERVERS; ++i) {
	printf("    server %i, client_port = %i, raft_port = %i, files_dir= %s \n", config.servers[i].id, ntohs(config.servers[i].client_socket.sin_port), ntohs(config.servers[i].raft_socket.sin_port), config.servers[i].file_directory);
    }
*/
    if(use_backup) {
	Raft_server_restore(&raft, files_dir, handle_raft_commit, id, port_raft); 
    } else {
	Raft_server_init(&raft, config, files_dir, handle_raft_commit, id, port_raft);
    }
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

