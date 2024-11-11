#include "client_rpc.h"
#include "packet_format.h"
#include "raft.h"
#include "udp.h"
#include <errno.h>
#include <stdio.h>

int send_packet(rpc_conn_t *rpc, packet_info_t *packet, response_info_t *response) {
    packet->vtime = rpc->vtime ++;
    packet->client_id = rpc->client_id;
    int rc = UDP_Write(rpc->sd, &rpc->raft_config.servers[rpc->current_leader_index].client_socket, (char*)packet, PACKET_SIZE);
    if(rc < 0) {
	printf("RPC:: failed to send packet");
	exit(1);
    }

    bzero(response, RESPONSE_SIZE);
    rc = UDP_Read(rpc->sd, &rpc->recv_addr, (char*)response, RESPONSE_SIZE);
    //printf("lock server: %s\n", response.message);
    int n_attempts = 1;
    while(1) {
	if(rc < 0 && (errno == ETIMEDOUT || errno == EAGAIN)) {
	    if(n_attempts >= RPC_RETRY_LIMIT) {
		// if retried too many times, switch to another server
		rpc->current_leader_index = (rpc->current_leader_index + 1) % N_SERVERS;
		n_attempts = 0;
	    }
	    rc = UDP_Write(rpc->sd, &rpc->raft_config.servers[rpc->current_leader_index].client_socket, (char*)packet, PACKET_SIZE);
	    rc = UDP_Read(rpc->sd, &rpc->recv_addr, (char*)response, RESPONSE_SIZE);
	    n_attempts ++;
	    continue;
	} else if(rc < 0) break;

	n_attempts = 0;

	if(response->rc == E_IN_PROGRESS || response->vtime < packet->vtime) {
	    rc = UDP_Read(rpc->sd, &rpc->recv_addr, (char*)response, RESPONSE_SIZE);
	} else if(response->rc == E_FOLLOWER || response->rc == E_ELECTION) {
	    if(response->rc == E_FOLLOWER) {
		rpc->current_leader_index = (rpc->current_leader_index + 1) % N_SERVERS;
	    }
	    rc = UDP_Write(rpc->sd, &rpc->raft_config.servers[rpc->current_leader_index].client_socket, (char*)packet, PACKET_SIZE);
	    rc = UDP_Read(rpc->sd, &rpc->recv_addr, (char*)response, RESPONSE_SIZE);
	} else break;
    }
    return rc;
}

void RPC_init(rpc_conn_t *rpc, int id, int src_port, raft_configuration_t raft_config){
    rpc->sd = UDP_Open(src_port);
    rpc->vtime = 0;
    rpc->client_id = id;
    rpc->raft_config = raft_config;
    rpc->current_leader_index = 0;
    UDP_SetReceiveTimeout(rpc->sd, RPC_READ_TIEMOUT);

    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = CLIENT_INIT;

    response_info_t response;
    if(send_packet(rpc, &packet, &response) < 0 || response.rc < 0) {
	printf("rpc error\n");
	exit(1);
    } 
}

void RPC_restore(rpc_conn_t *rpc, char *filename, int id, int src_port) {
    FILE *f = fopen(filename, "rb");
    fread(rpc, sizeof(rpc_conn_t), 1, f);
    fclose(f);

    rpc->sd = UDP_Open(src_port);
    UDP_SetReceiveTimeout(rpc->sd, RPC_READ_TIEMOUT);
    assert(rpc->client_id == id);
}

int RPC_acquire_lock(rpc_conn_t *rpc) {
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = LOCK_ACQUIRE;
   
    response_info_t response;
    if(send_packet(rpc, &packet, &response) < 0 || response.rc < 0) {
	printf("rpc error\n");
	return response.rc;
    } 

    memcpy(rpc->current_transaction, response.message, 2*sizeof(int));
    return 0;
}

int RPC_release_lock(rpc_conn_t *rpc){
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = LOCK_RELEASE;
    memcpy(packet.buffer, rpc->current_transaction, 2*sizeof(int));

    response_info_t response;
    int rc = send_packet(rpc, &packet, &response);
    if(rc < 0 || (response.rc < 0 && response.rc != E_LOCK_EXP)) {
	printf("rpc error: %i\n", (rc < 0) ? -1000 : response.rc);
	return rc < 0 ? rc : response.rc;
    }
    return 0;
}

int RPC_append_file(rpc_conn_t *rpc, char *file_name, char *buffer) {
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = APPEND_FILE;
    strcpy(packet.file_name, file_name);
    memcpy(packet.buffer, buffer, BUFFER_SIZE);

    response_info_t response;
    send_packet(rpc, &packet, &response);

    return response.rc;
} 

void RPC_close(rpc_conn_t *rpc){
    packet_info_t packet;
    bzero(&packet, PACKET_SIZE);
    packet.operation = CLIENT_CLOSE;

    response_info_t response;
    send_packet(rpc, &packet, &response);
} 

