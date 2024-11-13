#ifndef __RAFT_UTILS_h__
#define __RAFT_UTILS_h__

#include "raft.h"

raft_log_entry_t* Raft_get_log(raft_state_t *raft, int abs_index);

int Raft_get_log_term(raft_state_t *raft, int abs_index);

int Raft_absli2relli(raft_state_t *raft, int absolute_log_index);

int Raft_relli2absli(raft_state_t *raft, int relative_log_index);

void Raft_print_state(raft_state_t *raft);

#endif
