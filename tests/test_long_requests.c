#include <stdio.h>
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

    int use_backup = 0;
    if(argc > 1 && strcmp(argv[1], "use-backup") == 0) { 
	use_backup = 1;
    }
    raft_configuration_t config;
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    

   // fork server process
    int server_pid[N_SERVERS];
    for(int i = 0; i < N_SERVERS; ++i) {
	server_pid[i] = fork();
	if(server_pid[i] == 0) {
	    if(0) {
		exit(0);
	    }
	    char id_arg[2]; 
	    sprintf(id_arg, "%i", i+1);
	    char* args[] = {"./raft_config", id_arg, use_backup ? "use-backup" : NULL, NULL};
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
    
    char client_data_filename[128];
    sprintf(client_data_filename, "test_files/rpc_client_%i", client_id);

    // initialize RPCs for each client
    rpc_conn_t rpc;
    if(use_backup) {
	RPC_restore(&rpc, client_data_filename, client_id, 20000+client_id);	
    } else {
	RPC_init(&rpc, client_id, 20000 + client_id, config);
    }

    char* msg = malloc(BUFFER_SIZE); 
    sprintf(msg, "hello from client %i\n", client_id);
    
    char* buffer = malloc(BUFFER_SIZE);

    RPC_acquire_lock(&rpc); // acquire the lock

    for(int i = 0; i < 10; ++i) { // write msg to file_0 100 times

	char filename[7];
	sprintf(filename, "file_%i", i);
	for(int j = 0; j < strlen(msg); ++j) { // write a message character by character
	    buffer[0] = msg[j];
	    if(RPC_append_file(&rpc, filename, buffer) < 0) break;
	    //usleep(10000);
	}

        //RPC_release_lock(&rpc); // release the lock
    }
    if(RPC_release_lock(&rpc) == 0) {
	printf("transaction is committed, %i\n", client_id);
    } else {
	printf("transaction rejected\n");
    }
    // close RPC connections
    RPC_close(&rpc);

    f = fopen(client_data_filename, "wb");
    fwrite(&rpc, sizeof(rpc_conn_t), 1, f);
    fflush(f);
    fclose(f);

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
