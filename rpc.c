#include "rpc.h"
#include "packet_format.h"
#include "udp.h"
#include <stdio.h>

void send_packet(rpc_conn_t *rpc, packet_info_t *packet) {
    int rc = UDP_Write(rpc->sd, &rpc->send_addr, (char*)packet, PACKET_SIZE);
    if(rc < 0) {
	printf("RPC:: failed to send packet");
	exit(1);
    }

    response_info_t response;
    bzero(&response, RESPONSE_SIZE);
    rc = UDP_Read(rpc->sd, &rpc->recv_addr, (char*)&response, RESPONSE_SIZE);
    //printf("lock server: %s\n", response.message);
    if(rc < 0 || response.rc < 0) {
	printf("rpc:: server returned an error: %s\n", response.message);
	exit(1);
    }
    packet->rc = response.rc;

    // update client id if it was reassigned by the server
    rpc->client_id = response.client_id;
}

void RPC_init(rpc_conn_t *rpc, int src_port, int dst_port, char dst_addr[]) {
    rpc->sd = UDP_Open(src_port);
    UDP_FillSockAddr(&rpc->send_addr, dst_addr, dst_port);

    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = CLIENT_INIT;
    packet.client_id = -1; // if client id is -1, the server will assign an id

    send_packet(rpc, &packet); 
}

void RPC_acquire_lock(rpc_conn_t *rpc) {
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = LOCK_ACQUIRE;
   
    send_packet(rpc, &packet); 
}

void RPC_release_lock(rpc_conn_t *rpc){
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = LOCK_RELEASE;

    send_packet(rpc, &packet);
}

void RPC_append_file(rpc_conn_t *rpc, char *file_name, char *buffer) {
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = APPEND_FILE;
    strcpy(packet.file_name, file_name);
    memcpy(packet.buffer, buffer, BUFFER_SIZE);

    send_packet(rpc, &packet);
} 

void RPC_close(rpc_conn_t *rpc){
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = CLIENT_CLOSE;

    send_packet(rpc, &packet);
} 

