#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include "../client_rpc.h"
#include "../raft.h"
/*
Test lock behavior.

Creates one lock server process and 4 client processes that bind to distinct ports. Client processes simultaneously write messages "hello from client i" file_0 of the server, writing one character per request. By default, acquires a lock before starting writing each message instance and releases a lock once a message is written. However, if no-use-lock argument is passed, does not execute lock requests, and the final contents of file_0 mix all messages together.

*/


int main(int argc, char* argv[]) {
   /* 
    raft_configuration_t config;
    for(int i = 0; i < N_SERVERS; ++i) {
	config.servers[i].id = i+1;
	UDP_FillSockAddr(&config.servers[i].raft_socket, "localhost", 30000+i);
	UDP_FillSockAddr(&config.servers[i].client_socket, "localhost", 10000+i);
	sprintf(config.servers[i].files_directory, "./server_files_%i/", i+1);
    }

    FILE *file = fopen("./raft_config", "wb");
    fwrite(&config, sizeof(raft_configuration_t), 1, file);
    fclose(file);
   */

    raft_configuration_t config;
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);



   // fork server process
    int server_pid[N_SERVERS];
    for(int i = 0; i < N_SERVERS; ++i) {
	server_pid[i] = fork();
	if(server_pid[i] == 0) {
	    if(i == 0) {
		exit(0);
	    }
	    char id_arg[2]; 
	    sprintf(id_arg, "%i", i+1);
	    char* args[] = {"./raft_config", id_arg, NULL};
	    int rs = execv("./bin/server", args);
	    printf("exec failed, result: %i\n", rs);
	    exit(1);
	}
    }
    //while(1) {}

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

    RPC_acquire_lock(&rpc); // acquire the lock

    for(int i = 0; i < 10; ++i) { // write msg to file_0 100 times

	char filename[7];
	sprintf(filename, "file_%i", i);
	for(int j = 0; j < strlen(msg); ++j) { // write a message character by character
	    buffer[0] = msg[j];
	    RPC_append_file(&rpc, filename, buffer);
	    //usleep(10000);
	}

        //RPC_release_lock(&rpc); // release the lock
    }
    RPC_release_lock(&rpc);
    printf("here we think that the transaction is already committed, %i\n", client_id);
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
	while(1) {}
	for(int i = 0; i < N_SERVERS; ++i) {
	    kill(server_pid[i], SIGKILL);
	}
    }
}
