#ifndef __RAFT_CANDIDATE_h__
#define __RAFT_CANDIDATE_h__

#include "raft.h"

void Raft_convert_to_candidate(raft_state_t *raft);

void Raft_handle_vote_request(raft_state_t *raft, struct sockaddr_in *addr, raft_vote_request_t *vote_r);

int Raft_handle_vote_response(raft_state_t *raft, raft_response_packet_t *response); 

#endif
