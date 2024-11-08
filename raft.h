#ifndef __RAFT_h__
#define __RAFT_h__

#include "spinlock.h"
#include "udp.h"

#define __RAFT_h__
#define LOG_SIZE 1000
#define N_SERVERS 5 
#define MAX_SERVER_ID 10

#define ELECTION_TIMEOUT 1000
#define HEARTBIT_TIME 300

typedef struct raft_configuration {
	struct sockaddr_in servers[N_SERVERS];
	int ids[N_SERVERS];
} raft_configuration_t;

typedef struct raft_log_entry {
	int term;
	char* data;
} raft_log_entry_t;

typedef struct raft_state {
	// persistent state (updated on stable storage)
	raft_configuration_t config;
	int id;
	int current_term;
	int voted_for;
	raft_log_entry_t log[LOG_SIZE];
	int start_log_index;
	int log_count;

	// volatile state on all server
	enum node_state {
		LEADER,
		CANDIDATE,
		FOLLOWER
	} state;
	int rpc_sd;
	spinlock_t lock;
	int commit_index;
	int last_applied_index;

	// volatile state on candidates (initialized at the start of an election)
	int nvoted;

	// volatile state on leaders (initialized after an election)
	int next_index[MAX_SERVER_ID+1];
	int match_index[MAX_SERVER_ID+1];
	int last_request_id[MAX_SERVER_ID+1];
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


typedef enum request_type {
	APPEND,
	VOTE,
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
		raft_response_packet_t response;
	} data;
} raft_packet_t;


void Raft_server_init(raft_state_t *raft, raft_configuration_t config, int id, int port);

void Raft_RPC_listen(raft_state_t *raft);

#endif
