#ifndef __RAFT_FOLLOWER_h__
#define __RAFT_FOLLOWER_h__

#include "raft.h"

void Raft_convert_to_follower(raft_state_t *raft, int term);

void handle_raft_append_request(raft_state_t *raft, struct sockaddr_in *addr, raft_append_request_t *append_r);

void handle_raft_install_snapshot_request(raft_state_t *raft, struct sockaddr_in *addr, raft_install_snapshot_request_t *install_r);


#endif
