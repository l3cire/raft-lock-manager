#ifndef __RAFT_CANDIDATE_h__
#define __RAFT_CANDIDATE_h__

#include "raft.h"

int Raft_convert_to_candidate(raft_state_t *raft);

void* election_thread(void* arg);

void handle_raft_vote_request(raft_state_t *raft, struct sockaddr_in *addr, raft_vote_request_t *vote_r);

#endif
