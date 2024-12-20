#ifndef __RAFT_h__
#define __RAFT_h__

#include "spinlock.h"
#include "udp.h"
#include "packet_format.h"

#define __RAFT_h__

#define N_SERVERS 5 
#define MAX_SERVER_ID 10

#define LOG_SIZE 100
#define MAX_TRANSACTION_ENTRIES 10

#define COMMITS_TO_SNAPSHOT 60
#define SNAPSHOT_SIZE 50

#define ELECTION_TIMEOUT 1000
#define HEARTBIT_TIME 100

typedef struct raft_server_configuration {
	struct sockaddr_in client_socket;
	struct sockaddr_in raft_socket;
	char file_directory[128];
	int id;
} raft_server_configuration_t;

typedef struct raft_configuration {
	raft_server_configuration_t servers[N_SERVERS];
} raft_configuration_t;

typedef struct raft_transaction_entry {
	char filename[256];
	char buffer[BUFFER_SIZE];	
} raft_transaction_entry_t;

typedef struct raft_log_entry {
	int term;
	int n_servers_replicated;
	enum log_entry_type {
		CLIENT_LOG,
		LEADER_LOG
	} type;
	int id;
	int client;

	raft_transaction_entry_t data[MAX_TRANSACTION_ENTRIES];
} raft_log_entry_t;


typedef void (*raft_commit_handler)(raft_transaction_entry_t data[MAX_TRANSACTION_ENTRIES]);

typedef struct raft_state {
	// persistent state (updated on stable storage)
	raft_configuration_t config;
	int id;
	int current_term;
	int voted_for;
	raft_log_entry_t log[LOG_SIZE];
	int start_log_index;
	int log_count;
	char files_dir[256];

	// volatile state on all servers
	enum node_state {
		LEADER,
		CANDIDATE,
		FOLLOWER
	} state;
	int rpc_sd;
	spinlock_t lock;
	raft_commit_handler commit_handler;
	int commit_index;
	int last_applied_index;
	int snapshot_in_progress;

	int install_snapshot_index;
	int install_snapshot_id;

	// volatile state on candidates (initialized at the start of an election)
	int nvoted;
	int nblocked;

	// volatile state on leaders (initialized after an election)
	int next_index[MAX_SERVER_ID+1];
	int match_index[MAX_SERVER_ID+1];
	int last_request_id[MAX_SERVER_ID+1];
	int last_request_response[MAX_SERVER_ID+1];
	int n_followers_receiving_snapshots;
} raft_state_t;

typedef struct raft_append_request {
	int term;
	int leader_id;
	int prev_log_index;
	int prev_log_term;
	raft_log_entry_t entry;
	int entries_n;
	int leader_commit;
	int request_id;
} raft_append_request_t;

typedef struct raft_vote_request {
	int term;
	int candidate_id;
	int last_log_index;
	int last_log_term;
} raft_vote_request_t;

typedef struct raft_install_snapshot_request {
	int term;
	int leader_id;
	int snapshot_id;
	int index;
	int done;
	int request_id;

	char filename[256];
	char buffer[BUFFER_SIZE];
} raft_install_snapshot_request_t;


typedef enum request_type {
	APPEND,
	VOTE,
	INSTALL_SNAPSHOT,
	RESPONSE
} request_type_t;

typedef struct raft_response_packet {
	int id;
	int term;
	int success;

	int request_id;
} raft_response_packet_t;

typedef struct raft_packet {
	request_type_t request_type;
	union data {
		raft_append_request_t append_r;
		raft_vote_request_t vote_r;
		raft_install_snapshot_request_t install_r;
		raft_response_packet_t response;
	} data;
} raft_packet_t;


void Raft_server_restore(raft_state_t *raft, char filedir[256], raft_commit_handler commit_handler, int id, int port);

void Raft_server_init(raft_state_t *raft, raft_configuration_t config, char filedir[256], raft_commit_handler commit_handler, int id, int port);

void Raft_RPC_listen(raft_state_t *raft);

int Raft_append_entry(raft_state_t *raft, raft_log_entry_t *log); 

int Raft_is_entry_committed(raft_state_t *raft, int term, int id);

void Raft_commit_update(raft_state_t *raft, int new_commit_index);

#endif
