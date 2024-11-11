#ifndef __SERVER_RPC_h__
#define __SERVER_RPC_h__

#include "udp.h"
#include "packet_format.h"
#include "spinlock.h"
#include "raft.h"

#define MAX_ID 1000


// client_process_data
// stores the data of the client connected to the server
// created when init RPC is called and freed when close RPC is called 
//		state, vtime, and last_response are all protected by the lock
typedef struct client_process_data {
	int id;
	int state;
	int vtime;
	response_info_t last_response;
	spinlock_t lock;
} client_process_data_t;


typedef int (*lock_acquire_handler)(int client_id, char* response_message);
typedef int (*lock_release_handler)(int client_id, int transaction_term, int transaction_id, char* response_message);
typedef int (*append_file_handler)(int client_id, char* filename, char* buffer, char* response_message);


// RPC connection structure specifies handlers for different RPCs
//		client_table is protected by the client_table_lock: creating/accessing each element should be done while holding a lock
typedef struct server_rpc_conn {
	int sd;
	client_process_data_t* client_table[MAX_ID];
	spinlock_t client_table_lock;

	lock_acquire_handler handle_lock_acquire;
	lock_release_handler handle_lock_release;
	append_file_handler handle_append_file;

	raft_state_t *raft;
} server_rpc_conn_t;

typedef enum client_state {
	PROCESSING,
	WAITING
} client_state_t;

void Server_RPC_init(server_rpc_conn_t *rpc, raft_state_t *raft, int port);

void Server_RPC_listen(server_rpc_conn_t *rpc);

#endif
