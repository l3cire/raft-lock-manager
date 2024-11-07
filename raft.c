#include "raft.h"
#include "pthread.h"
#include "spinlock.h"
#include "udp.h"
#include <time.h>
#include <errno.h>

typedef struct raft_thread_arg {
    raft_state_t* raft;
    raft_packet_t packet;
    struct sockaddr_in addr; } raft_thread_arg_t;

void Raft_term_update(raft_state_t *raft, int new_term) {
    raft->current_term = new_term;
    raft->nvoted = 0;
    raft->voted_for = -1;
    raft->state = FOLLOWER;
}

void Raft_server_init(raft_state_t *raft, raft_configuration_t config, int id, int port) {
    raft->id = id;

    raft->rpc_sd = UDP_Open(port);
    UDP_SetReceiveTimeout(raft->rpc_sd, ELECTION_TIMEOUT); // election timeout will depend on a process;
    
    raft->config = config;
    raft->state = FOLLOWER;
    spinlock_init(&raft->lock);
    raft->voted_for = -1;
    raft->start_log_index = 0;
    raft->current_term = 0;
    
    raft->commit_index = 0;
    raft->log_count = 0;
    raft->last_applied_index = 0;
    raft->nvoted = 0;
}

void* handle_raft_packet(void* arg);
void* election_thread(void* arg);

void Raft_RPC_listen(raft_state_t *raft) {
    pthread_t req_thread_id;
    
    printf("(%i[%i]) starting server\n", raft->id, raft->current_term);
    while(1) {
	raft_thread_arg_t *arg = malloc(sizeof(raft_thread_arg_t));
	bzero(arg, sizeof(raft_thread_arg_t));
	arg->raft = raft;
	int rc = UDP_Read(raft->rpc_sd, &arg->addr, (char*)&arg->packet, sizeof(raft_packet_t));
	if(rc < 0 && (errno == ETIMEDOUT || errno == EAGAIN) && raft->state == FOLLOWER) {
	    spinlock_acquire(&raft->lock);
	    printf("(%i[%i]) follower timeout -- starting election\n", raft->id, raft->current_term);
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

void Raft_append_entries(raft_state_t *raft, char* entries[LOG_SIZE]) {

}

int Raft_convert_to_candidate(raft_state_t *raft) {
    // the lock must be held before calling that function!!!!!!
    raft->current_term ++;
    raft->voted_for = raft->id;
    raft->state = CANDIDATE;
    raft->nvoted = 1;
    
    raft_packet_t packet;
    bzero(&packet, sizeof(packet));
    packet.request_type = VOTE;
    packet.data.vote_r.term = raft->current_term;
    packet.data.vote_r.candidate_id = raft->id;
    packet.data.vote_r.last_log_index = raft->start_log_index + raft->log_count - 1;
    packet.data.vote_r.last_log_term = raft->log[raft->log_count-1].term;

    for(int i = 0; i < N_SERVERS; ++i) {
	if(raft->config.ids[i] == raft->id) continue;
	int rc = UDP_Write(raft->rpc_sd, &raft->config.servers[i], (char*)&packet, sizeof(packet));
	if(rc < 0) {
	    printf("server rpc sending error\n");
	    exit(1);
	}
    }

    spinlock_release(&raft->lock);

    clock_t election_start = clock();
    printf("(%i[%i]) about to start loop\n", raft->id, raft->current_term);
    while(1) {
	//printf("once more here %i\n", raft->id);
	spinlock_acquire(&raft->lock);
	//printf("here got the lock %i\n", raft->id);
	if(raft->state != CANDIDATE) {
	    printf("(%i[%i]) not a candidate anymore\n", raft->id, raft->current_term);
	    spinlock_release(&raft->lock);
	    return 0;
	}
	clock_t time_diff = clock() - election_start;
	int time_diff_msec = time_diff * 1000 / CLOCKS_PER_SEC;
	//printf("here (%i)\n", raft->id);
	if(time_diff_msec > ELECTION_TIMEOUT + 10*raft->id) {
	    printf("(%i[%i]) vote timeout -- restarting election\n", raft->id, raft->current_term);
	    return -1;
	}
	spinlock_release(&raft->lock);
	sched_yield();
    }
}

void Raft_convert_to_leader(raft_state_t *raft) {
    // the lock must be acquired here!!!!!!
    raft->state = LEADER;
    raft->voted_for = -1;
    
    for(int i = 0; i < N_SERVERS; ++i) {
	raft->next_index[i] = raft->log_count;
	raft->match_index[i] = 0;
    }

    raft_packet_t heartbeat;
    bzero(&heartbeat, sizeof(raft_packet_t));
    heartbeat.request_type = APPEND;
    heartbeat.data.append_r.term = raft->current_term;
    heartbeat.data.append_r.entries_n = 0;
    heartbeat.data.append_r.leader_id = raft->id;

    printf("(%i[%i]) elected as leader\n", raft->id, raft->current_term);

    while(1) {
	if(raft->state != LEADER) {
	    spinlock_release(&raft->lock);
	    break;
	}
	for(int i = 0; i < N_SERVERS; ++i) {
	    if(raft->config.ids[i] == raft->id) continue;
	    int rc = UDP_Write(raft->rpc_sd, &raft->config.servers[i], (char*)&heartbeat, sizeof(heartbeat));
	    if(rc < 0) {
		printf("server rpc sending error\n");
		exit(1);
	    }
	}
	printf("(%i[%i]) leader sent one heartbeat -- entering infinite loop \n", raft->id, raft->current_term);
	while(1) {}
	spinlock_release(&raft->lock);
	//TODO:: sleep(1)
	spinlock_acquire(&raft->lock);
    }
}

void handle_raft_response(raft_state_t *raft, raft_response_packet_t *response) {
    spinlock_acquire(&raft->lock);

    printf("	[%i -> %i] responded %i (terms %i -> %i)\n", response->id, raft->id, response->success, response->term, raft->current_term);
    if(raft->current_term > response->term) {
	spinlock_release(&raft->lock);
	return;
    }
    if(raft->current_term < response->term) {
	printf("(%i) GOT BEHIND, SWITCHING TO FOLLOWER\n", raft->current_term);
	raft->current_term = response->term;
	raft->state = FOLLOWER;
	raft->voted_for = -1;
	spinlock_release(&raft->lock);
	return;
    }

    if(raft->state == CANDIDATE) {
	if(!response->success) {
	    spinlock_release(&raft->lock);
	    return;
	}
	raft->nvoted ++;
	printf("(%i[%i]) nvoted = %i\n", raft->id, raft->current_term, raft->nvoted);
	if(raft->nvoted*2 > N_SERVERS) {
	    Raft_convert_to_leader(raft);
	    return;
	}
    } else if(raft->state == LEADER) {

    }

    spinlock_release(&raft->lock);
}

void handle_raft_vote_request(raft_state_t *raft, struct sockaddr_in *addr, raft_vote_request_t *vote_r) {
    spinlock_acquire(&raft->lock);

    printf("	[%i -> %i] request vote\n", vote_r->candidate_id, raft->id);
    if(raft->current_term < vote_r->term) {
	raft->current_term = vote_r->term;
	raft->state = FOLLOWER;
	raft->voted_for = -1;
    }

    raft_packet_t packet;
    bzero(&packet, sizeof(raft_packet_t));
    packet.request_type = RESPONSE;
    packet.data.response.id = raft->id;
    packet.data.response.term = raft->current_term;
    packet.data.response.success = 0;
    
    if(raft->current_term == vote_r->term && (raft->voted_for == -1 || raft->voted_for == vote_r->candidate_id)) {
	int last_log_term = raft->log[raft->log_count - 1].term;
	int last_log_index = raft->start_log_index + raft->log_count - 1; 
	if(vote_r->last_log_term > last_log_term) {
	    packet.data.response.success = 1;
	    raft->voted_for = vote_r->candidate_id;
	} else if(vote_r->last_log_term == last_log_term && vote_r->last_log_index >= last_log_index) {
	    packet.data.response.success = 1;
	    raft->voted_for = vote_r->candidate_id;
	}
    }

    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));

    spinlock_release(&raft->lock);
}

void handle_raft_append_request(raft_state_t *raft, struct sockaddr_in *addr, raft_append_request_t *append_r) {
    spinlock_acquire(&raft->lock);

    printf("	[%i -> %i] append request\n", append_r->leader_id, raft->id);
    printf("(%i[%i]) entering infinite loop\n", raft->id, raft->current_term);
    while(1) {}
        
    if(raft->current_term < append_r->term) {
	raft->current_term = append_r->term;
	raft->state = FOLLOWER;
	raft->voted_for = -1;
    }

    raft_packet_t packet;
    bzero(&packet, sizeof(raft_packet_t));
    packet.request_type = RESPONSE;
    packet.data.response.id = raft->id;
    packet.data.response.term = raft->current_term;

    
    if(raft->current_term == append_r->term && append_r->entries_n == 0) {
	//TODO:: reset timer
	packet.data.response.success = 1;
    }else if(raft->current_term > append_r->term || (raft->log[append_r->prev_log_index - raft->start_log_index].term != append_r->prev_log_term)) {
	packet.data.response.success = 0;
    } else {
	int index = append_r->prev_log_index - raft->start_log_index + 1;
	if(raft->log_count - 1 >= index && raft->log[index].term != append_r->term) {
	    raft->log_count = index + 1;
    	}
	raft->log[index] = append_r->entry;
	if(append_r->leader_commit > raft->commit_index) {
	    raft->commit_index = (append_r->leader_commit > append_r->prev_log_index+1) ? append_r->prev_log_index + 1 : append_r->leader_commit;
	}
	packet.data.response.success = 1;
    } 
    
    UDP_Write(raft->rpc_sd, addr, (char*)&packet, sizeof(raft_packet_t));

    spinlock_release(&raft->lock);
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
    }


    free(arg);
    pthread_exit(0);
}

void* election_thread(void* arg) {
    raft_state_t *raft = (raft_state_t*)arg;
    while(Raft_convert_to_candidate(raft) != 0);
    pthread_exit(0);
}

