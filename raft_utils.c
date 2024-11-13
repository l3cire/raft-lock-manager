#include "raft.h"
#include "raft_utils.h"

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

