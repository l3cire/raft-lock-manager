#ifndef __FORMAT_h__
#define __FORMAT_h__

#define BUFFER_SIZE 1024

typedef enum operation_type{
	CLIENT_INIT,
	LOCK_ACQUIRE,
	LOCK_RELEASE,
	APPEND_FILE,
	CLIENT_CLOSE
} operation_type_t;

typedef enum response_code {
	E_FILE = -1,
	E_IN_PROGRESS = -2,
	E_NO_CLIENT = -3,
	E_LOCK = -4,
	E_LOCK_EXP = -5,
	E_TRANSACTION_LIMIT = -6,
	E_FOLLOWER = -7,
	E_ELECTION = -8,
	E_LOST = -9
} response_code_t;

typedef struct packet_info{
	int client_id; //unique number for each client
	int vtime;
	operation_type_t operation; //RPC operation
	char file_name[256]; //file name
	char buffer[BUFFER_SIZE]; //data appending to the file
} packet_info_t;

typedef struct response_info {
	int client_id;
	int rc;
	int vtime;
	char message[256];
} response_info_t;

#define PACKET_SIZE sizeof(packet_info_t)
#define RESPONSE_SIZE sizeof(response_info_t)

#endif
