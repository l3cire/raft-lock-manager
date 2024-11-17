#ifndef __RAFT_STORAGE_MANAGER_h__
#define __RAFT_STORAGE_MANAGER_h__

#include "packet_format.h"
#include "raft.h"

void Raft_load_state(raft_state_t *raft, char filedir[256]);

void Raft_save_state(raft_state_t *raft);

void Raft_create_snapshot_dir(raft_state_t *raft, int snapshot_id);

void Raft_remove_snapshot(raft_state_t *raft, int snapshot_id);

void Raft_clean_main_files(raft_state_t *raft);

void Raft_copy_snapshot(raft_state_t *raft, int source_snapshot_id, int dest_snapshot_id);

void Raft_add_to_snapshot(raft_state_t *raft, int snapshot_id, int create_new_sn, char filename[256], char buffer[BUFFER_SIZE]);

typedef struct snapshot_iterator {
	FILE *f;
	int fileno;
	int snapshot_id;
	raft_state_t *raft;
} snapshot_iterator_t;

void snapshot_it_init(snapshot_iterator_t *it, raft_state_t *raft, int snapshot_id);

int snapshot_it_get_next(snapshot_iterator_t *it, char filename[256], char buffer[BUFFER_SIZE]);

#endif
