#include "raft.h"
#include "pthread.h"
#include "spinlock.h"
#include "udp.h"
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "raft_storage_manager.h"

typedef struct raft_thread_arg {
    raft_state_t* raft;
    raft_packet_t packet;
    struct sockaddr_in addr; } raft_thread_arg_t;

typedef struct raft_leader_thread_arg {
    raft_state_t* raft;
    int follower_id;
    struct sockaddr_in addr;
} raft_leader_thread_arg_t;

void Raft_print_state(raft_state_t *raft) {
    char state_str[256];
    sprintf(state_str, "%i(%i)	%i[", raft->id, raft->current_term, raft->start_log_index);
    for(int i = 0; i < raft->log_count - raft->start_log_index; ++i) {
	if(raft->log[i].type == LEADER_LOG) {
	    sprintf(state_str + strlen(state_str), "l");
	}
	sprintf(state_str + strlen(state_str), "%i(%i)", i + raft->start_log_index, raft->log[i].term);
	if(i + raft->start_log_index <= raft->commit_index) {
	    sprintf(state_str + strlen(state_str), "c");
	}
	if(i < raft->log_count - raft->start_log_index - 1) {
	    sprintf(state_str + strlen(state_str), ", ");
	}
    }
    sprintf(state_str + strlen(state_str), "]\n");
    printf("%s", state_str);
}


void Raft_term_update(raft_state_t *raft, int new_term) {
    raft->current_term = new_term;
    raft->nvoted = 0;
    raft->nblocked = 0;
    raft->voted_for = -1;
    raft->state = FOLLOWER;

    if(raft->install_snapshot_id != -1) {
	Raft_remove_snapshot(raft, raft->install_snapshot_id);
	raft->install_snapshot_id = -1;
	raft->install_snapshot_index = -1;
    }
}

raft_log_entry_t* Raft_get_log(raft_state_t *raft, int abs_index) {
    if(abs_index < raft->start_log_index || abs_index >= raft->log_count) {
	return 0;
    }
    return &raft->log[abs_index - raft->start_log_index];
}

int Raft_get_log_term(raft_state_t *raft, int abs_index) {
    if(abs_index < raft->start_log_index || abs_index >= raft->log_count) {
	return -1;
    }
    return raft->log[abs_index - raft->start_log_index].term;
}

int Raft_absli2relli(raft_state_t *raft, int absolute_log_index) {
    if(absolute_log_index < raft->start_log_index || absolute_log_index >= raft->start_log_index + LOG_SIZE) return -1;
    return absolute_log_index - raft->start_log_index;
}

int Raft_relli2absli(raft_state_t *raft, int relative_log_index) {
    return relative_log_index + raft->start_log_index;
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

void Raft_commit_update(raft_state_t *raft, int new_commit_index) {
    for(int i = raft->commit_index + 1; i <= new_commit_index; ++i) {
	if(Raft_get_log(raft, i)->type == LEADER_LOG) continue;
	raft->commit_handler(Raft_get_log(raft, i)->data);
    }
    raft->commit_index = new_commit_index;
}


int Raft_create_snapshot(raft_state_t *raft, int new_log_start) {
    spinlock_acquire(&raft->lock);
    if(raft->snapshot_in_progress || new_log_start <= raft->start_log_index || new_log_start > raft->commit_index + 1) {
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
    
    raft->commit_index = -1;
    raft->log_count = 0;
    raft->last_applied_index = -1;
    raft->nvoted = 0;
    raft->nblocked = 0;
    strcpy(raft->files_dir, filedir);

    raft->install_snapshot_id = -1;
    raft->install_snapshot_index = -1;

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
}


void* handle_raft_packet(void* arg);
void* election_thread(void* arg);

void Raft_RPC_listen(raft_state_t *raft) {
    pthread_t req_thread_id;
    
    //printf("(%i[%i]) starting server\n", raft->id, raft->current_term);
    while(1) {
	raft_thread_arg_t *arg = malloc(sizeof(raft_thread_arg_t));
	bzero(arg, sizeof(raft_thread_arg_t));
	arg->raft = raft;
	int rc = UDP_Read(raft->rpc_sd, &arg->addr, (char*)&arg->packet, sizeof(raft_packet_t));
	if(rc < 0 && (errno == ETIMEDOUT || errno == EAGAIN) && raft->state == FOLLOWER) {
	    //printf("(%i[%i]) follower timeout -- starting election\n", raft->id, raft->current_term);
	    spinlock_acquire(&raft->lock);
	    free(arg);
	    pthread_create(&req_thread_id, NULL, election_thread, raft);
	    pthread_detach(req_thread_id);
	} else if(rc < 0) {
	    free(arg);
	} else {
	    pthread_create(&req_thread_id, NULL, handle_raft_packet, arg);
	    pthread_detach(req_thread_id);
	}
    }
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


int Raft_convert_to_candidate(raft_state_t *raft) {
    // the lock must be held before calling that function!!!!!!
    raft->current_term ++;
    raft->voted_for = raft->id;
    raft->state = CANDIDATE;
    raft->nvoted = 1;
    raft->nblocked = 0;
    Raft_save_state(raft);
    
    raft_packet_t packet;
    bzero(&packet, sizeof(packet));
    packet.request_type = VOTE;
    packet.data.vote_r.term = raft->current_term;
    packet.data.vote_r.candidate_id = raft->id;
    packet.data.vote_r.last_log_index = raft->log_count-1; 
    packet.data.vote_r.last_log_term = Raft_get_log_term(raft, raft->log_count-1);

    for(int i = 0; i < N_SERVERS; ++i) {
	if(raft->config.servers[i].id == raft->id) continue;
	UDP_Write(raft->rpc_sd, &raft->config.servers[i].raft_socket, (char*)&packet, sizeof(packet));
    }

    spinlock_release(&raft->lock);

    clock_t election_start = clock();
    //printf("(%i[%i]) about to start loop\n", raft->id, raft->current_term);
    int timeout = ELECTION_TIMEOUT + (rand() % 100);
    while(1) {
	////printf("once more here %i\n", raft->id);
	spinlock_acquire(&raft->lock);
	////printf("here got the lock %i\n", raft->id);
	if(raft->state != CANDIDATE) {
	    //printf("(%i[%i]) not a candidate anymore\n", raft->id, raft->current_term);
	    spinlock_release(&raft->lock);
	    return 0;
	}
	clock_t time_diff = clock() - election_start;
	int time_diff_msec = time_diff * 1000 / CLOCKS_PER_SEC;
	////printf("here (%i)\n", raft->id);
	if(time_diff_msec > timeout && raft->nblocked*2 < N_SERVERS) {
	    //printf("(%i[%i]) vote timeout -- restarting election\n", raft->id, raft->current_term);
	    return -1;
	}
	spinlock_release(&raft->lock);
	sched_yield();
    }
}

void Raft_send_snapshot(raft_state_t *raft, int follower_id, struct sockaddr_in *addr) {
    //wait for all snapshots to finish
    while(raft->snapshot_in_progress) {
	spinlock_release(&raft->lock);
	sched_yield();
	spinlock_acquire(&raft->lock);
    }

    raft->snapshot_in_progress = 1; // set snapshot flag so that no snapshots are created

    int id = raft->start_log_index;
    int ind = 0;

    raft_packet_t packet;
    packet.request_type = INSTALL_SNAPSHOT;
    packet.data.install_r.snapshot_id = id;
    packet.data.install_r.term = raft->current_term;
    packet.data.install_r.leader_id = raft->id;

    snapshot_iterator_t it;
    snapshot_it_init(&it, raft, id);

    while(snapshot_it_get_next(&it, packet.data.install_r.filename, packet.data.install_r.buffer)) {
	packet.data.install_r.index = ind;
	ind++;
	packet.data.install_r.request_id = raft->last_request_id[follower_id];
	packet.data.install_r.done = 0;
	while(packet.data.install_r.request_id == raft->last_request_id[follower_id]) {
	    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));
	    spinlock_release(&raft->lock);
	    usleep(HEARTBIT_TIME*1000);
	    spinlock_acquire(&raft->lock);
	}
    }
   
    packet.data.install_r.done = 1;
    packet.data.install_r.request_id = raft->last_request_id[follower_id];
    packet.data.install_r.index = ind;

    while(packet.data.install_r.request_id == raft->last_request_id[follower_id]) {
	UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));
	spinlock_release(&raft->lock);
	usleep(HEARTBIT_TIME*1000);
	spinlock_acquire(&raft->lock);
    }

    raft->next_index[follower_id] = raft->start_log_index;
    raft->match_index[follower_id] = raft->start_log_index - 1;
    raft->snapshot_in_progress = 0;

    printf("SUCCESSFULLY INSTALLED A SNAPSHOT\n");
}

void Raft_send_append_entry_request(raft_state_t *raft, int follower_id, struct sockaddr_in *addr) {
    raft_packet_t packet;
    packet.request_type = APPEND;
    packet.data.append_r.term = raft->current_term;
    packet.data.append_r.leader_id = raft->id;
    packet.data.append_r.leader_commit = raft->commit_index;

    int next_ind = raft->next_index[follower_id];
    packet.data.append_r.prev_log_index = next_ind - 1;
    packet.data.append_r.prev_log_term = Raft_get_log_term(raft, next_ind - 1); 
    packet.data.append_r.request_id = raft->last_request_id[follower_id];
    if(next_ind < raft->start_log_index) {
	printf("TRYING TO GET A SNAPSHOTTED LOG ENTRY: %i for server %i\n", next_ind, follower_id);
	Raft_send_snapshot(raft, follower_id, addr);
	return;
    }
    if(next_ind == raft->log_count) {
	packet.data.append_r.entries_n = 0;
	//printf("(%i) sending heartbeat to %i\n", raft->id, follower_id);
    } else {
	packet.data.append_r.entries_n = 1;
	packet.data.append_r.entry = *Raft_get_log(raft, next_ind); 
	//printf("(%i) appending entry (%i, %i), count = %i\n", raft->id, follower_id, next_ind, packet.data.append_r.entries_n);
    }

    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));
    //printf("rc = %i = siseof = %i\n", rc, (int)sizeof(request));
}

void* Raft_leader_thread(void* arg) {
    raft_state_t *raft = ((raft_leader_thread_arg_t*)arg)->raft;
    int follower_id = ((raft_leader_thread_arg_t*)arg)->follower_id;
    struct sockaddr_in *addr = &((raft_leader_thread_arg_t*)arg)->addr;

    while(1) {
	spinlock_acquire(&raft->lock);
	if(raft->state != LEADER) {
	    spinlock_release(&raft->lock);
	    break;
	}
	Raft_send_append_entry_request(raft, follower_id, addr);
	spinlock_release(&raft->lock);
	usleep(HEARTBIT_TIME*1000);
    }
    
    free(arg);
    pthread_exit(0);
}


void Raft_convert_to_leader(raft_state_t *raft) {
    // the lock must be acquired here!!!!!!
    raft->state = LEADER;
    raft->voted_for = -1;

    // adding an artificial log entry in order to commit all previous ones
    raft->log_count ++;
    raft_log_entry_t *log = Raft_get_log(raft, raft->log_count - 1);
    log->term = raft->current_term;
    log->n_servers_replicated = 1;
    log->type = LEADER_LOG;

    
    for(int i = 0; i <= MAX_SERVER_ID; ++i) {
	raft->next_index[i] = raft->log_count;
	raft->match_index[i] = 0;
	raft->last_request_id[i] = 0;
    }
    
    Raft_print_state(raft);
    printf("(%i[%i]) elected as leader\n", raft->id, raft->current_term);

    Raft_save_state(raft);

    spinlock_release(&raft->lock);

    for(int i = 0; i < N_SERVERS; ++i) {
	if(raft->config.servers[i].id == raft->id) continue;
	raft_leader_thread_arg_t *arg = malloc(sizeof(raft_leader_thread_arg_t));
	arg->raft = raft; arg->follower_id = raft->config.servers[i].id; arg->addr = raft->config.servers[i].raft_socket;
	pthread_t tid;
	pthread_create(&tid, NULL, Raft_leader_thread, arg);
    }
}

void handle_raft_response(raft_state_t *raft, raft_response_packet_t *response) {
    spinlock_acquire(&raft->lock);

    //printf("	[%i -> %i] responded %i (terms %i -> %i)\n", response->id, raft->id, response->success, response->term, raft->current_term);
    if(raft->current_term > response->term) {
	spinlock_release(&raft->lock);
	return;
    }
    if(raft->current_term < response->term) {
	//printf("(%i) GOT BEHIND, SWITCHING TO FOLLOWER\n", raft->current_term);
	Raft_term_update(raft, response->term);
	Raft_save_state(raft);
	spinlock_release(&raft->lock);
	return;
    }

    if(raft->state == CANDIDATE) {
	if(response->success <= 0) {
	    if(response->success == -1) raft->nblocked ++;
	    spinlock_release(&raft->lock);
	    return;
	}
	raft->nvoted ++;
	//printf("(%i[%i]) nvoted = %i\n", raft->id, raft->current_term, raft->nvoted);
	if(raft->nvoted*2 > N_SERVERS) {
	    Raft_convert_to_leader(raft);
	    return;
	}
    } else if(raft->state == LEADER && response->request_id == raft->last_request_id[response->id]) {
	if(raft->next_index[response->id] < raft->start_log_index) {
	    if(!response->success) {
		printf("fatal error installing snapshot\n");
		exit(1);
	    }
	} else if(!response->success) {
	    raft->next_index[response->id] --;
	} else if(raft->next_index[response->id] < raft->log_count) {
	    if(raft->next_index[response->id] > raft->commit_index &&
		Raft_get_log_term(raft, raft->next_index[response->id]) == raft->current_term &&
		(++Raft_get_log(raft, raft->next_index[response->id])->n_servers_replicated)*2 > N_SERVERS) {
		
		Raft_commit_update(raft, raft->next_index[response->id]);
		//Raft_print_state(raft);
	    }
	    raft->match_index[response->id] = raft->next_index[response->id];
	    raft->next_index[response->id] ++;
	}
	raft->last_request_id[response->id] ++;
	Raft_save_state(raft);
    }

    spinlock_release(&raft->lock);
}

void handle_raft_vote_request(raft_state_t *raft, struct sockaddr_in *addr, raft_vote_request_t *vote_r) {
    spinlock_acquire(&raft->lock);

    //printf("	[%i -> %i] request vote\n", vote_r->candidate_id, raft->id);
    if(raft->current_term < vote_r->term) {
	Raft_term_update(raft, vote_r->term);
	Raft_save_state(raft);
    }

    raft_packet_t packet;
    bzero(&packet, sizeof(raft_packet_t));
    packet.request_type = RESPONSE;
    packet.data.response.id = raft->id;
    packet.data.response.term = raft->current_term;
    packet.data.response.success = 0;
    packet.data.response.request_id = -1;
    
    if(raft->current_term == vote_r->term && raft->voted_for == -1) {
	int last_log_term = Raft_get_log_term(raft, raft->log_count - 1); 
	int last_log_index = raft->log_count - 1; 
	if(vote_r->last_log_term > last_log_term) {
	    packet.data.response.success = 1;
	    raft->voted_for = vote_r->candidate_id;
	} else if(vote_r->last_log_term == last_log_term && vote_r->last_log_index >= last_log_index) {
	    packet.data.response.success = 1;
	    raft->voted_for = vote_r->candidate_id;
	} else {
	    packet.data.response.success = -1;
	}
	Raft_save_state(raft);
    }

    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));

    spinlock_release(&raft->lock);
}

void handle_raft_append_request(raft_state_t *raft, struct sockaddr_in *addr, raft_append_request_t *append_r) {
    spinlock_acquire(&raft->lock);

    //printf("	[%i -> %i] append request\n", append_r->leader_id, raft->id);
    //printf("(%i[%i]) entering infinite loop\n", raft->id, raft->current_term);
    if(raft->current_term < append_r->term) {
	Raft_term_update(raft, append_r->term);
    }

    raft_packet_t packet;
    bzero(&packet, sizeof(raft_packet_t));
    packet.request_type = RESPONSE;
    packet.data.response.id = raft->id;
    packet.data.response.term = raft->current_term;
    packet.data.response.request_id = append_r->request_id;

    
    if(raft->current_term > append_r->term || 
		(append_r->prev_log_index >= raft->log_count) || 
		(Raft_get_log_term(raft, append_r->prev_log_index) != append_r->prev_log_term)) {
	packet.data.response.success = 0;
	//printf("    (%i) consistency check failed for prev_index = %i (term %i)\n", raft->id, append_r->prev_log_index, append_r->prev_log_term);
    } else if(append_r->entries_n == 0) { // this means we are consistent 
	raft->state = FOLLOWER;
	packet.data.response.success = 1;
	if(append_r->leader_commit > raft->commit_index) {
	    Raft_commit_update(raft, append_r->leader_commit);
	}
    } else {
	raft->state = FOLLOWER;
	int index = append_r->prev_log_index + 1;
	if(index < raft->log_count && Raft_get_log_term(raft, index) != append_r->entry.term) { // rewrite log entries contradicting with new one
	    raft->log_count = index + 1;
    	} else if (raft->log_count == index) {
	    raft->log_count ++;
	}
	*Raft_get_log(raft, index) = append_r->entry;
	if(append_r->leader_commit > raft->commit_index) {
	    Raft_commit_update(raft, (append_r->leader_commit > index) ? index : append_r->leader_commit);
	}
	//printf("    (%i) replicated position %i with term value %i\n", raft->id, append_r->prev_log_index + 1, raft->log[index].term);
	packet.data.response.success = 1;
    } 

    //Raft_print_state(raft);
    Raft_save_state(raft);
    
    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));

    spinlock_release(&raft->lock);
}


void handle_raft_install_snapshot_request(raft_state_t *raft, struct sockaddr_in *addr, raft_install_snapshot_request_t *install_r) {
    spinlock_acquire(&raft->lock);

    if(raft->current_term < install_r->term) {
	Raft_term_update(raft, install_r->term);
	Raft_save_state(raft);
    }

 
    raft_packet_t packet;
    bzero(&packet, sizeof(raft_packet_t));
    packet.request_type = RESPONSE;
    packet.data.response.id = raft->id;
    packet.data.response.term = raft->current_term;
    packet.data.response.request_id = install_r->request_id;

    int outdated_snapshot = 0;
    
    if(raft->current_term > install_r->term || 
	(raft->install_snapshot_id != -1 && raft->install_snapshot_id != install_r->snapshot_id) || 
	raft->install_snapshot_index + 1 != install_r->index) {
	    packet.data.response.success = 0;
    } else if(install_r->done) {
	raft->snapshot_in_progress = 0;
	outdated_snapshot = raft->start_log_index;
	printf("outdated snapshot: %i\n", outdated_snapshot);
	raft->start_log_index = install_r->snapshot_id;
	raft->log_count = raft->start_log_index;
	raft->commit_index = raft->start_log_index - 1;
	raft->last_applied_index = raft->start_log_index - 1;

	raft->install_snapshot_index = -1;
	raft->install_snapshot_id = -1;
	
	Raft_copy_snapshot(raft, raft->start_log_index, -1);
	Raft_save_state(raft);

	packet.data.response.success = 1;
    } else {
	Raft_add_to_snapshot(raft, install_r->snapshot_id, (raft->install_snapshot_id == -1), install_r->filename, install_r->buffer);	
	raft->snapshot_in_progress = 1;
	raft->install_snapshot_id = install_r->snapshot_id;
	raft->install_snapshot_index++;

	Raft_save_state(raft);

	packet.data.response.success = 1;
    }
    
    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));    

    spinlock_release(&raft->lock);

    if(outdated_snapshot != 0) {
	Raft_remove_snapshot(raft, outdated_snapshot);
    }
}

void* handle_raft_packet(void* arg) {
    raft_packet_t *packet = &((raft_thread_arg_t*)arg)->packet;
    struct sockaddr_in *addr = &((raft_thread_arg_t*)arg)->addr;
    raft_state_t *raft = ((raft_thread_arg_t*)arg)->raft;

    switch (packet->request_type) {
	case RESPONSE:
	    handle_raft_response(raft, &packet->data.response); 
	    break;
	case VOTE:
	    handle_raft_vote_request(raft, addr, &packet->data.vote_r);
	    break;
	case APPEND:
	    handle_raft_append_request(raft, addr, &packet->data.append_r);
	    break;
	case INSTALL_SNAPSHOT:
	    handle_raft_install_snapshot_request(raft, addr, &packet->data.install_r);
	    break;
    }

    if(raft->commit_index - raft->start_log_index + 1 >= COMMITS_TO_SNAPSHOT) {
	Raft_create_snapshot(raft, raft->commit_index - 2);
    }

    free(arg);
    pthread_exit(0);
}

void* election_thread(void* arg) {
    raft_state_t *raft = (raft_state_t*)arg;
    while(Raft_convert_to_candidate(raft) != 0);
    pthread_exit(0);
}

