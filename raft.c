#include "raft.h"
#include "raft_leader.h"
#include "raft_utils.h"
#include "raft_storage_manager.h"
#include "raft_candidate.h"
#include "raft_follower.h"
#include "pthread.h"
#include "spinlock.h"
#include "udp.h"
#include <stdlib.h>
#include <time.h>
#include <errno.h>

typedef struct raft_packet_thread_arg {
    raft_state_t* raft;
    raft_packet_t packet;
    struct sockaddr_in addr; } raft_packet_thread_arg_t;

void Raft_server_init(raft_state_t *raft, raft_configuration_t config, char filedir[256], raft_commit_handler commit_handler, int id, int port) {
    raft->id = id;

    raft->rpc_sd = UDP_Open(port);
    srand(time(0));
    UDP_SetReceiveTimeout(raft->rpc_sd, ELECTION_TIMEOUT +  (rand() % 100)); // election timeout will depend on a process;
    raft->commit_handler = commit_handler;

    raft->config = config;
    raft->state = FOLLOWER;
    spinlock_init(&raft->lock);
    raft->voted_for = -1;
    raft->start_log_index = 0;
    raft->current_term = 0;
    raft->snapshot_in_progress = 0;
    raft->n_followers_receiving_snapshots = 0;
    
    raft->commit_index = -1;
    raft->log_count = 0;
    raft->last_applied_index = -1;
    raft->nvoted = 0;
    raft->nblocked = 0;
    strcpy(raft->files_dir, filedir);

    raft->install_snapshot_id = -1;
    raft->install_snapshot_index = -1;

    for(int i = 0; i < 100; ++i) Raft_remove_snapshot(raft, i);

}

void Raft_server_restore(raft_state_t *raft, char filedir[256], raft_commit_handler commit_handler, int id, int port) {
    Raft_load_state(raft, filedir); 
    assert(raft->id == id);

    raft->rpc_sd = UDP_Open(port);
    srand(time(0));
    UDP_SetReceiveTimeout(raft->rpc_sd, ELECTION_TIMEOUT + (rand() % 100)); // election timeout will depend on a process;
    raft->commit_handler = commit_handler;
    raft->state = FOLLOWER;
    raft->snapshot_in_progress = 0;
    raft->n_followers_receiving_snapshots = 0;

    spinlock_init(&raft->lock);
    
    int prev_session_commit_index = raft->commit_index;
    raft->commit_index = raft->start_log_index - 1;
    raft->last_applied_index = -1;
    raft->nvoted = 0;
    raft->nblocked = 0;
    strcpy(raft->files_dir, filedir);

    raft->install_snapshot_id = -1;
    raft->install_snapshot_index = -1;

    if(raft->start_log_index != 0) {
	Raft_copy_snapshot(raft, raft->start_log_index, -1);
    }
    Raft_commit_update(raft, prev_session_commit_index);

    for(int i = 0; i < 100; ++i) {
	if(i == raft->start_log_index) continue;
	Raft_remove_snapshot(raft, i);
    }
}


int Raft_is_entry_committed(raft_state_t *raft, int term, int id) {
    spinlock_acquire(&raft->lock);
    for(int i = raft->start_log_index; i < raft->log_count; ++i) {
	raft_log_entry_t *log = Raft_get_log(raft, i);
	if(log->term == term && log->id == id) {
	    int res = (raft->commit_index >= i) ? 1 : 0;
	    spinlock_release(&raft->lock);
	    return res;
	} else if(log->term > term) {
	    int res = (raft->commit_index >= i) ? -1 : 0;
	    spinlock_release(&raft->lock);
	    return res;
	}
    }
    spinlock_release(&raft->lock);
    return 0;
}

int Raft_append_entry(raft_state_t *raft, raft_log_entry_t *log) { 
    spinlock_acquire(&raft->lock);
    if(raft->state != LEADER || raft->log_count == raft->start_log_index + LOG_SIZE) {
	spinlock_release(&raft->lock);
	return -1;
    } 

    raft->log_count ++;
    log->term = raft->current_term;
    log->n_servers_replicated = 1;
    log->type = CLIENT_LOG;
    memcpy(Raft_get_log(raft, raft->log_count-1), log, sizeof(raft_log_entry_t));
    Raft_save_state(raft);

    spinlock_release(&raft->lock);
    return 0;
}


void Raft_commit_update(raft_state_t *raft, int new_commit_index) {
    for(int i = raft->commit_index + 1; i <= new_commit_index; ++i) {
	if(Raft_get_log(raft, i)->type == LEADER_LOG) continue;
	raft->commit_handler(Raft_get_log(raft, i)->data);
    }
    raft->commit_index = new_commit_index;
}

int Raft_create_snapshot(raft_state_t *raft, int new_log_start) {
    spinlock_acquire(&raft->lock);
    if(raft->snapshot_in_progress || raft->n_followers_receiving_snapshots > 0 || new_log_start <= raft->start_log_index || new_log_start > raft->commit_index + 1) {
	spinlock_release(&raft->lock);
	return -1;
    }
    raft->snapshot_in_progress = 1; // set the flag that the snapshot is in progress
    spinlock_release(&raft->lock);

    Raft_create_snapshot_dir(raft, new_log_start);

    int prev_snap_id = raft->start_log_index;
    if(raft->start_log_index != 0) {
	Raft_copy_snapshot(raft, raft->start_log_index, new_log_start);
    }


    for(int i = raft->start_log_index; i < new_log_start; ++i) {
	raft_log_entry_t *log = Raft_get_log(raft, i);
	if(log->type == LEADER_LOG) continue;
	for(int j = 0; j < MAX_TRANSACTION_ENTRIES; ++j) {
	    if(log->data[j].filename[0] == 0) break;
	    Raft_add_to_snapshot(raft, new_log_start, 0, log->data[j].filename, log->data[j].buffer);
	}
    }

    spinlock_acquire(&raft->lock);
    raft->snapshot_in_progress = 0;
    for(int i = new_log_start; i < raft->log_count; ++i) {
	raft->log[i - new_log_start] = raft->log[i - raft->start_log_index];
    }
    raft->start_log_index = new_log_start;
    Raft_save_state(raft);

    spinlock_release(&raft->lock);

    if(prev_snap_id != 0) {
	Raft_remove_snapshot(raft, prev_snap_id);
    }
    
    return 0;
}


void Raft_handle_response(raft_state_t *raft, raft_response_packet_t *response) {
    spinlock_acquire(&raft->lock);
    //printf("	[%i -> %i] responded %i (terms %i -> %i)\n", response->id, raft->id, response->success, response->term, raft->current_term);
    if(raft->current_term > response->term) {
	spinlock_release(&raft->lock);
	return;
    }
    if(raft->current_term < response->term) {
	Raft_convert_to_follower(raft, response->term);
	Raft_save_state(raft);
	spinlock_release(&raft->lock);
	return;
    }

    raft->last_request_response[response->id] = response->success;

    if(raft->state == CANDIDATE) {
	if(Raft_handle_vote_response(raft, response)) return;	
    } else if(raft->state == LEADER && response->request_id == raft->last_request_id[response->id]) {
	if(raft->next_index[response->id] < raft->start_log_index) {
	    Raft_handle_install_response(raft, response);
	} else {
	    Raft_handle_append_response(raft, response);
	}
    }
    spinlock_release(&raft->lock);
}

void* Raft_handle_packet(void* arg) {
    raft_packet_t *packet = &((raft_packet_thread_arg_t*)arg)->packet;
    struct sockaddr_in *addr = &((raft_packet_thread_arg_t*)arg)->addr;
    raft_state_t *raft = ((raft_packet_thread_arg_t*)arg)->raft;

    switch (packet->request_type) {
	case RESPONSE: 
	    Raft_handle_response(raft, &packet->data.response); 
	    break;
	case VOTE:
	    Raft_handle_vote_request(raft, addr, &packet->data.vote_r);
	    break;
	case APPEND:
	    Raft_handle_append_request(raft, addr, &packet->data.append_r);
	    break;
	case INSTALL_SNAPSHOT:
	    Raft_handle_install_snapshot_request(raft, addr, &packet->data.install_r);
	    break;
    }

    if(raft->commit_index - raft->start_log_index + 1 >= COMMITS_TO_SNAPSHOT) {
	Raft_create_snapshot(raft, raft->commit_index - 2);
    }

    free(arg);
    pthread_exit(0);
}

void Raft_RPC_listen(raft_state_t *raft) {
    pthread_t req_thread_id;
    
    //printf("(%i[%i]) starting server\n", raft->id, raft->current_term);
    while(1) {
	raft_packet_thread_arg_t *arg = malloc(sizeof(raft_packet_thread_arg_t));
	bzero(arg, sizeof(raft_packet_thread_arg_t));
	arg->raft = raft;
	int rc = UDP_Read(raft->rpc_sd, &arg->addr, (char*)&arg->packet, sizeof(raft_packet_t));
	if(rc < 0 && (errno == ETIMEDOUT || errno == EAGAIN) && raft->state == FOLLOWER) {
	    //printf("(%i[%i]) follower timeout -- starting election\n", raft->id, raft->current_term);
	    spinlock_acquire(&raft->lock);
	    free(arg);
	    Raft_convert_to_candidate(raft);
	} else if(rc < 0) {
	    free(arg);
	} else {
	    pthread_create(&req_thread_id, NULL, Raft_handle_packet, arg);
	    pthread_detach(req_thread_id);
	}
    }
}

