#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define N_CLIENTS 4
#define N_SERVERS 5

char* clients[N_CLIENTS] = {
    "./bin/client_long_requests",
    "./bin/client_mult_sessions",
    "./bin/client_no_release",
    "./bin/client_normal"
};

int main(int argc, char* argv[]) {
    int use_backup = 0;
    if(argc > 1 && strcmp(argv[1], "use-backup") == 0) { 
	use_backup = 1;
    }
    printf("use_backup = %s\n", (use_backup) ? "true" : "false");

   // fork server process
    int server_pid[N_SERVERS];
    for(int i = 0; i < N_SERVERS; ++i) {
	server_pid[i] = fork();
	if(server_pid[i] != 0) continue; 

	char id_arg[2]; sprintf(id_arg, "%i", i+1);
	char* args[] = {"./raft_config", id_arg, use_backup ? "use-backup" : NULL, NULL};
	int rs = execv("./bin/server", args);
	printf("exec failed, result: %i\n", rs);
	exit(1);
    }

    // wait one second to make sure the server has started receiving requests
    sleep(1);

    int client_pid[N_CLIENTS];
    for(int i = 0; i < N_CLIENTS; ++i) {
	client_pid[i] = fork();
	if(client_pid[i] != 0) continue;

	char id_arg[2]; sprintf(id_arg, "%i", i);
	char port_arg[5]; sprintf(port_arg, "%i", 2000+i);
	char* args[] = {"./raft_config", id_arg, port_arg, NULL};
	int rs = execv(clients[i], args);
	printf("client exec failed, result: %i\n", rs);
    }

    int status;
    for(int i = 0; i < N_CLIENTS; ++i) {
	waitpid(client_pid[i], &status, 0);
    }

    printf("clients finished\n");

    while(1) {}

}
