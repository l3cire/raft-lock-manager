#include <pthread.h>
#include <stdio.h>
#include "packet_format.h"
#include "udp.h"

void* handle_packet(void *arg);

int sd;

typedef struct request {
    packet_info_t packet;
    struct sockaddr_in addr;
} request_t;

int main(int argc, char *argv[]) {
    sd = UDP_Open(10000);
    assert(sd >= 0);

    pthread_t req_thread_id;

    while(1) {
	request_t request;
	int rc = UDP_Read(sd, &request.addr, (char*)&request.packet, PACKET_SIZE);
	if(rc < 0) continue;
	printf("got request\n");
	pthread_create(&req_thread_id, NULL, handle_packet, &request);
	pthread_detach(req_thread_id);
    }
}

void send_packet_response(struct sockaddr_in *addr, int response_code) {
    UDP_Write(sd, addr, (char*)&response_code, sizeof(response_code));
}

void* handle_packet(void *arg) {
    packet_info_t *packet = &((request_t*)arg)->packet;
    struct sockaddr_in *addr = &((request_t*)arg)->addr;

    switch (packet->operation) {
	case CLIENT_INIT:
	    send_packet_response(addr, 0);
	    break;
	case LOCK_ACQUIRE:
	    break;
	case LOCK_RELEASE:
	    break;
	case APPEND_FILE:
	    break;
	case CLIENT_CLOSE:
	    send_packet_response(addr, 0);
	    break;
    }
    pthread_exit(0);
}
