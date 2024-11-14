#include "../raft.h"
#include "../client_rpc.h"

int main(int argc, char* argv[]) {
    raft_configuration_t config;
    FILE *f = fopen(argv[0], "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);
    
    int id = atoi(argv[1]);
    int port = atoi(argv[2]);

    rpc_conn_t rpc;
    RPC_init(&rpc, id, port, config);


    char* msg = malloc(BUFFER_SIZE); 
    sprintf(msg, "multiple sessions client message (id %i)\n", id);
    
    char* buffer = malloc(BUFFER_SIZE);


    for(int k = 0; k < 20; ++k) { // write msg to file_0 100 times
	RPC_acquire_lock(&rpc); // acquire the lock
	for(int i = 0; i < 10; ++i) {

	    char filename[7];
	    sprintf(filename, "file_%i", i);
	    for(int j = 0; j < strlen(msg); ++j) { // write a message character by character
		buffer[0] = msg[j];
		RPC_append_file(&rpc, filename, buffer);
	    }
	}
	RPC_release_lock(&rpc);
    }

    RPC_close(&rpc);
    return 0;
}

