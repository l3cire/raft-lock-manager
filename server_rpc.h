#ifndef __SERVER_RPC_h__
#define __SERVER_RPC_h__

#include "udp.h"
#include "packet_format.h"
#include "spinlock.h"

typedef int (*lock_acquire_handler)(int client_id, char* response_message);
typedef int (*lock_release_handler)(int client_id, char* response_message);
typedef int (*append_file_handler)(int client_id, char* filename, char* buffer, char* response_message);

// RPC connection structure specifies handlers for different RPCs
typedef struct server_rpc_conn {
	int sd;
	lock_acquire_handler handle_lock_acquire;
	lock_release_handler handle_lock_release;
	append_file_handler handle_append_file;

} server_rpc_conn_t;

typedef struct client_process_data {
	int id;
	int state;
	int vtime;
	response_info_t last_response;
	spinlock_t lock;
} client_process_data_t;

typedef enum client_state {
	PROCESSING,
	WAITING
} client_state_t;

void Server_RPC_init(server_rpc_conn_t *rpc, int port);

void Server_RPC_listen(server_rpc_conn_t *rpc);

#endif
