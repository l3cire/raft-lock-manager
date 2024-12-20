#include "server_rpc.h"
#include "raft.h"
#include "udp.h"
#include "packet_format.h"
#include "spinlock.h"
#include <pthread.h>

typedef struct request {
    server_rpc_conn_t *rpc;
    packet_info_t packet;
    struct sockaddr_in addr;
} request_t;


void Server_RPC_init(server_rpc_conn_t *rpc, raft_state_t *raft, int port) {
    rpc->sd = UDP_Open(port);
    rpc->raft = raft;
    
    spinlock_init(&rpc->client_table_lock);
    bzero(rpc->client_table, sizeof(rpc->client_table));
}

void* handle_packet(void *arg);

void Server_RPC_listen(server_rpc_conn_t *rpc) {
    pthread_t req_thread_id;
    
    while(1) {
	request_t *request = malloc(sizeof(request_t));
	bzero(request, sizeof(request_t));
	request->rpc = rpc;
	int rc = UDP_Read(rpc->sd, &request->addr, (char*)&request->packet, PACKET_SIZE);
	if(rc < 0) continue;

	pthread_create(&req_thread_id, NULL, handle_packet, request);
	pthread_detach(req_thread_id);
    }
}

int send_packet_response(server_rpc_conn_t *rpc, struct sockaddr_in *addr, response_info_t *response) {
    return UDP_Write(rpc->sd, addr, (char*)response, RESPONSE_SIZE);
}

void* handle_packet(void *arg) {
    packet_info_t *packet = &((request_t*)arg)->packet;
    struct sockaddr_in *addr = &((request_t*)arg)->addr;
    server_rpc_conn_t *rpc = ((request_t*)arg)->rpc;

    response_info_t response;
    bzero(&response, RESPONSE_SIZE);
    response.client_id = packet->client_id;
    response.vtime = packet->vtime;

    if(rpc->raft->state != LEADER) {
	response.rc = (rpc->raft->state == FOLLOWER) ? E_FOLLOWER : E_ELECTION;
	sprintf(response.message, "this is not the leader server; address another one\n");
	send_packet_response(rpc, addr, &response);
	free(arg);
	pthread_exit(0);
    }
    
    // get the client data structure -- if it does not exist and the request is init, create a new structure;
    spinlock_acquire(&rpc->client_table_lock);
    if(rpc->client_table[packet->client_id] == 0) {
	// if client not initialized and the operation is not init, return an error
	/*if(packet->operation != CLIENT_INIT) {
	    spinlock_release(&rpc->client_table_lock);
	    response.rc = E_NO_CLIENT;
	    sprintf(response.message, "client not initialized");
	    send_packet_response(rpc, addr, &response);
	    free(arg);
	    pthread_exit(0);
	}*/
	rpc->client_table[packet->client_id] = malloc(sizeof(client_process_data_t));
	rpc->client_table[packet->client_id]->id = packet->client_id;
	rpc->client_table[packet->client_id]->vtime = -1;
	spinlock_init(&rpc->client_table[packet->client_id]->lock);
    } 
    client_process_data_t* client = rpc->client_table[packet->client_id];
    spinlock_release(&rpc->client_table_lock); // at this point, got a client structure pointer and don't need a whole table lock anymore
    

    spinlock_acquire(&client->lock); // lock the client state to do all the necessary checks
    if(client->vtime == packet->vtime) {
	if(client->state == PROCESSING) { 
	    // if the request from the client is already processed on another thread, return the corresponding message
	    response.rc = E_IN_PROGRESS;
	    sprintf(response.message, "request from this client is already in progress");
	    send_packet_response(rpc, addr, &response);
	} else {
	    // if already sent the response for this request before, repeat this response
	    send_packet_response(rpc, addr, &client->last_response);
	}
	spinlock_release(&client->lock);
	free(arg);
	pthread_exit(0);
    } 
    client->state = PROCESSING;
    client->vtime = packet->vtime;
    spinlock_release(&client->lock);

    switch (packet->operation) {
	case CLIENT_INIT:
	    strcpy(response.message, "connected"); // TODO: check user didn't exist before
	    break;
	case LOCK_ACQUIRE:
	    response.rc = rpc->handle_lock_acquire(packet->client_id, response.message);
	    break;
	case LOCK_RELEASE: {
	    int transaction_data[2];
	    memcpy(transaction_data, packet->buffer, 2*sizeof(int));
	    response.rc = rpc->handle_lock_release(packet->client_id, transaction_data[0], transaction_data[1], response.message);
	    break;
	}
	case APPEND_FILE:
	    response.rc = rpc->handle_append_file(packet->client_id, packet->file_name, packet->buffer, response.message);
	    break;
	case CLIENT_CLOSE:
	    strcpy(response.message, "disconnected"); // TODO: clear user's data
	    break;
    }

    spinlock_acquire(&client->lock);
    client->state = WAITING;
    client->last_response = response;
    send_packet_response(rpc, addr, &response);
    spinlock_release(&client->lock);

    free(arg);
    pthread_exit(0);
}

