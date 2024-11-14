#include "raft.h"
#include "raft_utils.h"
#include "raft_storage_manager.h"
#include "raft_leader.h"

#include <pthread.h>

void Raft_send_snapshot(raft_state_t *raft, int follower_id, struct sockaddr_in *addr) {
    //wait for all snapshots to finish
    printf("TRYING TO SEND A SNAPSHOT TO %i\n", follower_id);
    while(raft->snapshot_in_progress) {
	spinlock_release(&raft->lock);
	sched_yield();
	spinlock_acquire(&raft->lock);
    }

    raft->n_followers_receiving_snapshots ++; // set snapshot flag so that no snapshots are created

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
	if(!raft->last_request_response[follower_id]) {
	    printf("ABORTING SENDING A SNAPSHOT\n");
	    raft->n_followers_receiving_snapshots --;
	    return;
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
    if(!raft->last_request_response[follower_id]) {
	printf("ABORTING SENDING A SNAPSHOT\n");
	raft->n_followers_receiving_snapshots --;
	return;
    }

    raft->next_index[follower_id] = raft->start_log_index;
    raft->match_index[follower_id] = raft->start_log_index - 1;
    raft->n_followers_receiving_snapshots --;

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
	if(raft->next_index[follower_id] < raft->start_log_index) {
	    Raft_send_snapshot(raft, follower_id, addr);
	} else {
	    Raft_send_append_entry_request(raft, follower_id, addr);
	}
	spinlock_release(&raft->lock);
	usleep(HEARTBIT_TIME*1000);
    }
    
    free(arg);
    pthread_exit(0);
}

void Raft_convert_to_leader(raft_state_t *raft) {
    // the lock must be acquired here!!!!!!
    raft->state = LEADER;
    raft->n_followers_receiving_snapshots = 0;

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
	raft->last_request_response[i] = -1;
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

void Raft_handle_append_response(raft_state_t *raft, raft_response_packet_t *response) {
    if(!response->success) {
	raft->next_index[response->id] --;
    } else if(raft->next_index[response->id] < raft->log_count) {
	if(raft->next_index[response->id] > raft->commit_index && Raft_get_log_term(raft, raft->next_index[response->id]) == raft->current_term &&
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

void Raft_handle_install_response(raft_state_t *raft, raft_response_packet_t *response) {
    if(!response->success) {
	printf("error installing snapshot (follower probably relaunched)\n");
    }
    raft->last_request_id[response->id] ++;
    Raft_save_state(raft);
}

