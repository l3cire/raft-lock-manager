#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "packet_format.h"
#include "udp.h"
#include "spinlock.h"

spinlock_t lock;
int sd;

typedef struct request {
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

void* handle_packet(void *arg);

int main(int argc, char *argv[]) {
    sd = UDP_Open(10000);
    assert(sd >= 0);

    spinlock_init(&lock);

    spinlock_init(&ids_lock);
    ids = malloc(MAX_ID);
    bzero(ids, MAX_ID);

    pthread_t req_thread_id;

    while(1) {
	request_t *request = malloc(sizeof(request_t));
	bzero(request, sizeof(request_t));
	int rc = UDP_Read(sd, &request->addr, (char*)&request->packet, PACKET_SIZE);
	if(rc < 0) continue;
	pthread_create(&req_thread_id, NULL, handle_packet, request);
	pthread_detach(req_thread_id);
    }
}

int send_packet_response(struct sockaddr_in *addr, response_info_t *response) {
    return UDP_Write(sd, addr, (char*)response, RESPONSE_SIZE);
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

int handle_lock_acquire(response_info_t *response, packet_info_t *packet) {
    spinlock_acquire(&lock);
    response->rc = 0;
    strcpy(response->message, "lock acquired");
    return 0;
}

int handle_lock_release(response_info_t *response, packet_info_t *packet) {
    spinlock_release(&lock);
    response->rc = 0;
    strcpy(response->message, "lock released");
    return 0;
}

int handle_append_file(response_info_t *response, packet_info_t *packet) {
    char filename[25] = "./server_files/";
    strcat(filename, packet->file_name);
    FILE *f = fopen(filename, "a");
    if(!f) {
	response->rc = -1;
	strcpy(response->message, "file cannot be opened");
	return 0;
    }
    fprintf(f, "%s", packet->buffer);
    fclose(f);
    response->rc = 0;
    strcpy(response->message, "success");
    return 0;
}

void* handle_packet(void *arg) {
    packet_info_t *packet = &((request_t*)arg)->packet;
    struct sockaddr_in *addr = &((request_t*)arg)->addr;

    response_info_t response;
    bzero(&response, RESPONSE_SIZE);
    response.operation = packet->operation;
    response.client_id = packet->client_id;
    switch (packet->operation) {
	case CLIENT_INIT:
	    handle_client_init(&response, packet);
	    break;
	case LOCK_ACQUIRE:
	    handle_lock_acquire(&response, packet);
	    break;
	case LOCK_RELEASE:
	    handle_lock_release(&response, packet);
	    break;
	case APPEND_FILE:
	    handle_append_file(&response, packet);
	    break;
	case CLIENT_CLOSE:
	    handle_client_close(&response, packet);
	    break;
    }
    send_packet_response(addr, &response);
    free(arg);
    pthread_exit(0);
}

