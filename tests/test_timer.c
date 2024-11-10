#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include "../client_rpc.h"

/*
Test lock behavior.

Creates one lock server process and 4 client processes that bind to distinct ports. Client processes simultaneously write messages "hello from client i" file_0 of the server, writing one character per request. By default, acquires a lock before starting writing each message instance and releases a lock once a message is written. However, if no-use-lock argument is passed, does not execute lock requests, and the final contents of file_0 mix all messages together.

*/


int main(int argc, char* argv[]) {
    raft_configuration_t config;
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);


    // fork server process
    int server_pid = fork();
    if(server_pid == 0) {
	int rs = execv("./bin/server", 0);
	printf("exec failed, result: %i\n", rs);
	exit(1);
    }

    // wait one second to make sure the server has started receiving requests
    sleep(1);

    // fork 4 client processes
    int a = fork();
    int b = fork();
    int client_id = (a > 0) ? ((b > 0) ? 0 : 1) : ((b > 0) ? 2 : 3);

    // initialize RPCs for each client
    rpc_conn_t rpc;
    RPC_init(&rpc, client_id, 20000 + client_id, config);
    
    char* msg = malloc(BUFFER_SIZE); 
    sprintf(msg, "hello from client %i\n", client_id);
    
    char* buffer = malloc(BUFFER_SIZE);

    RPC_acquire_lock(&rpc);
    for(int i = 0; i < 100; ++i) { // write msg to file_0 100 times
	for(int j = 0; j < strlen(msg); ++j) { // write a message character by character
	    buffer[0] = msg[j];
	    RPC_append_file(&rpc, "file_0", buffer);
	}
    }

    // close RPC connections
    RPC_close(&rpc);

    // wait for all children clients to finish writing
    int status;
    if(a > 0) {
	waitpid(a, &status, 0);
    }

    if(b > 0) {
	waitpid(b, &status, 0);
    }

    // if we are the original process, kill the server and exit
    if(a > 0 && b > 0) {
	kill(server_pid, SIGKILL);
    }
}
