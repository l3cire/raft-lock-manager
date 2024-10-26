#ifndef __RPC_h__
#define __RPC_h__

#include "udp.h"
#include "packet_format.h"

typedef struct rpc_conn {
	int sd;
	struct sockaddr_in send_addr;
	struct sockaddr_in recv_addr;
	int client_id;
} rpc_conn_t;


//This function should set up a socket and bind it to src_port
void RPC_init(rpc_conn_t *rpc, int src_port, int dst_port, char dst_addr[]); 

void RPC_acquire_lock(rpc_conn_t *rpc);

void RPC_release_lock(rpc_conn_t *rpc);

void RPC_append_file(rpc_conn_t *rpc, char *file_name, char *buffer); 

void RPC_close(rpc_conn_t *rpc); 

#endif



