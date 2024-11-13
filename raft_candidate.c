#include "raft.h"
#include "raft_candidate.h"
#include "raft_utils.h"
#include "raft_storage_manager.h"
#include "raft_follower.h"

#include <pthread.h>

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
	spinlock_acquire(&raft->lock);
	if(raft->state != CANDIDATE) {
	    //printf("(%i[%i]) not a candidate anymore\n", raft->id, raft->current_term);
	    spinlock_release(&raft->lock);
	    return 0;
	}
	clock_t time_diff = clock() - election_start;
	int time_diff_msec = time_diff * 1000 / CLOCKS_PER_SEC;
	if(time_diff_msec > timeout && raft->nblocked*2 < N_SERVERS) {
	    //printf("(%i[%i]) vote timeout -- restarting election\n", raft->id, raft->current_term);
	    return -1;
	}
	spinlock_release(&raft->lock);
	sched_yield();
    }
}

void* election_thread(void* arg) {
    raft_state_t *raft = (raft_state_t*)arg;
    while(Raft_convert_to_candidate(raft) != 0);
    pthread_exit(0);
}

void handle_raft_vote_request(raft_state_t *raft, struct sockaddr_in *addr, raft_vote_request_t *vote_r) {
    spinlock_acquire(&raft->lock);

    //printf("	[%i -> %i] request vote\n", vote_r->candidate_id, raft->id);
    if(raft->current_term < vote_r->term) {
	Raft_convert_to_follower(raft, vote_r->term);
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

