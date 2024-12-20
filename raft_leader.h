#ifndef __RAFT_LEADER_h__
#define __RAFT_LEADER_h__

#include "raft.h"

typedef struct raft_leader_thread_arg {
    raft_state_t* raft;
    int follower_id;
    struct sockaddr_in addr;
} raft_leader_thread_arg_t;

void Raft_convert_to_leader(raft_state_t *raft);

void Raft_handle_append_response(raft_state_t *raft, raft_response_packet_t *response);

void Raft_handle_install_response(raft_state_t *raft, raft_response_packet_t *response);

#endif
