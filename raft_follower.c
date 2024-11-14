#include "raft.h"
#include "raft_follower.h"
#include "raft_utils.h"
#include "raft_storage_manager.h"

void Raft_convert_to_follower(raft_state_t *raft, int term) {
    raft->current_term = term;
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

void Raft_handle_append_request(raft_state_t *raft, struct sockaddr_in *addr, raft_append_request_t *append_r) {
    spinlock_acquire(&raft->lock);

    //printf("	[%i -> %i] append request\n", append_r->leader_id, raft->id);
    //printf("(%i[%i]) entering infinite loop\n", raft->id, raft->current_term);
    if(raft->current_term < append_r->term) {
	Raft_convert_to_follower(raft, append_r->term);
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

void Raft_handle_install_snapshot_request(raft_state_t *raft, struct sockaddr_in *addr, raft_install_snapshot_request_t *install_r) {
    spinlock_acquire(&raft->lock);

    if(raft->current_term < install_r->term) {
	Raft_convert_to_follower(raft, install_r->term);
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
	    printf("PROBLEM WITH INSTALL REQUEST: terms [%i -> %i], snap ids [%i -> %i], snap_inds [%i -> %i]\n", raft->current_term, install_r->term, raft->install_snapshot_id, install_r->snapshot_id, raft->install_snapshot_index, install_r->index);
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

