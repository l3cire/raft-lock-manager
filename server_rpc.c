#include "server_rpc.h"
#include "udp.h"
#include "packet_format.h"
#include "spinlock.h"
#include <pthread.h>

typedef struct request {
    server_rpc_conn_t *rpc;
    packet_info_t packet;
    struct sockaddr_in addr;
} request_t;

#define MAX_ID 1000
char* ids;
spinlock_t ids_lock;

int assign_id() {
    spinlock_acquire(&ids_lock);
    for(int i = 0; i < MAX_ID; ++i) {
	if(ids[i] == 0) {
	    ids[i] = 1;
	    spinlock_release(&ids_lock);
	    return i;
	}
    }
    spinlock_release(&ids_lock);
    return -1;
}

void release_id(int id) {
    spinlock_acquire(&ids_lock);
    ids[id] = 0;
    spinlock_release(&ids_lock);
}


void Server_RPC_init(server_rpc_conn_t *rpc, int port) {
    rpc->sd = UDP_Open(port);
    
    spinlock_init(&ids_lock);
    ids = malloc(MAX_ID);
    bzero(ids, MAX_ID);
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

int handle_client_init(response_info_t *response, packet_info_t *packet) {
    response->client_id = assign_id();
    if(response->client_id == -1) {
	response->rc = -1;
	strcpy(response->message, "too many clients");
	return 0;
    }
    response->rc = 0;
    strcpy(response->message, "connected");
    return 0;
}

int handle_client_close(response_info_t *response, packet_info_t *packet) {
    release_id(response->client_id);
    response->rc = 0;
    strcpy(response->message, "disconnected");
    return 0;
}

void* handle_packet(void *arg) {
    packet_info_t *packet = &((request_t*)arg)->packet;
    struct sockaddr_in *addr = &((request_t*)arg)->addr;
    server_rpc_conn_t *rpc = ((request_t*)arg)->rpc;

    response_info_t response;
    bzero(&response, RESPONSE_SIZE);
    response.operation = packet->operation;
    response.client_id = packet->client_id;
    switch (packet->operation) {
	case CLIENT_INIT:
	    handle_client_init(&response, packet);
	    break;
	case LOCK_ACQUIRE:
	    response.rc = rpc->handle_lock_acquire(packet->client_id, response.message);
	    break;
	case LOCK_RELEASE:
	    response.rc = rpc->handle_lock_release(packet->client_id, response.message);
	    break;
	case APPEND_FILE:
	    response.rc = rpc->handle_append_file(packet->client_id, packet->file_name, packet->buffer, response.message);
	    break;
	case CLIENT_CLOSE:
	    handle_client_close(&response, packet);
	    break;
    }
    send_packet_response(rpc, addr, &response);
    free(arg);
    pthread_exit(0);
}

