#include "packet_format.h"
#include "raft_storage_manager.h"
#include "raft.h"
#include <stdio.h>
#include <sys/stat.h>

void Raft_load_state(raft_state_t *raft, char filedir[256]) {
    char raft_file[256];
    sprintf(raft_file,  "%sraft_state", filedir);
    FILE *f = fopen(raft_file, "rb");
    fread(raft, sizeof(raft_state_t), 1, f);
    fclose(f);
}

void Raft_save_state(raft_state_t *raft) {
    char tmp_raft_file[256];
    strcpy(tmp_raft_file, raft->files_dir);
    strcat(tmp_raft_file, "tmp_raft_state");
    FILE *f = fopen(tmp_raft_file, "wb");
    fwrite(raft, sizeof(raft_state_t), 1, f);
    fflush(f);
    fclose(f);

    char raft_file[256];
    strcpy(raft_file, raft->files_dir);
    strcat(raft_file, "raft_state");
    rename(tmp_raft_file, raft_file);
}

int Raft_get_snapshot_path(raft_state_t *raft, int id, char path[256]) {
    if(id == -1) {
	sprintf(path, "%s", raft->files_dir);
    } else {
	sprintf(path, "%ssnapshot_%i/", raft->files_dir, id);
    }
    return strlen(path);
}

void Raft_create_snapshot_dir(raft_state_t *raft, int snapshot_id) {
    char dir[256];
    Raft_get_snapshot_path(raft, snapshot_id, dir);
    mode_t mod = 0777;
    mkdir(dir, mod);
}

void Raft_remove_snapshot(raft_state_t *raft, int snapshot_id) {
    char path[256];
    int path_len = Raft_get_snapshot_path(raft, snapshot_id, path);

    for(int file_ind = 0; file_ind < 100; ++file_ind) {
	sprintf(path + path_len, "file_%i", file_ind);
	remove(path);
    }
    Raft_get_snapshot_path(raft, snapshot_id, path);
    rmdir(path);
}

void Raft_clean_main_files(raft_state_t *raft) {
    char path[256];
    strcpy(path, raft->files_dir);
    for(int file_ind = 0; file_ind < 100; ++file_ind) {
	sprintf(path + strlen(raft->files_dir), "file_%i", file_ind);
	FILE *f = fopen(path, "w");
	fclose(f);
    }
}

void Raft_copy_snapshot(raft_state_t *raft, int source_snapshot_id, int dest_snapshot_id) {
    char dir1[256], dir2[256];
    int dir1_len = Raft_get_snapshot_path(raft, source_snapshot_id, dir1);
    int dir2_len = Raft_get_snapshot_path(raft, dest_snapshot_id, dir2);
    

    for(int file_ind = 0; file_ind < 100; ++file_ind) {
	sprintf(dir1 + dir1_len, "file_%i", file_ind);
    	FILE *f1 = fopen(dir1, "r");
	if(f1 == NULL) continue;

	sprintf(dir2 + dir2_len, "file_%i", file_ind);
	FILE *f2 = fopen(dir2, "w");

	int a;
	while((a = fgetc(f1)) != EOF) {
	    fputc(a, f2);
	}
	fclose(f1);
	fclose(f2);
    }
}

void Raft_add_to_snapshot(raft_state_t *raft, int snapshot_id, int create_new_sn, char filename[256], char buffer[BUFFER_SIZE]) {
    char dir[256];
    Raft_get_snapshot_path(raft, snapshot_id, dir);
    if(create_new_sn) {
        mode_t mod = 0777;
	mkdir(dir, mod);
    }
    
    sprintf(dir + strlen(dir), "%s", filename);
    FILE *f = fopen(dir, "a+");
    fprintf(f, "%s", buffer);
    fflush(f);
    fclose(f);
}


void snapshot_it_init(snapshot_iterator_t *it, raft_state_t *raft, int snapshot_id) {
    it->fileno = -1;
    it->f = NULL;
    it->raft = raft;
    it->snapshot_id = snapshot_id;
}

int snapshot_it_get_next(snapshot_iterator_t *it, char filename[256], char buffer[BUFFER_SIZE]) {
    char dir[256];
    int dir_len = Raft_get_snapshot_path(it->raft, it->snapshot_id, dir);
    while(it->f == NULL) {
	it->fileno++;
	if(it->fileno >= 100) return 0;
	
	sprintf(dir + dir_len, "file_%i", it->fileno);
	it->f = fopen(dir, "rb");
    }

    sprintf(filename, "file_%i", it->fileno);

    bzero(buffer, BUFFER_SIZE);
    int nread = fread(buffer, sizeof(char), BUFFER_SIZE, it->f);
    if(nread < BUFFER_SIZE) {
	fclose(it->f);
	it->f = NULL;
    }
    return 1;
}


