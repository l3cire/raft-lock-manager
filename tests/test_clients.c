#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "./server_cluster.c"

#define N_CLIENTS 10

char* clients[N_CLIENTS] = {
    "./bin/client_long_requests",
    "./bin/client_mult_sessions",
    "./bin/client_no_release",
    "./bin/client_normal",
    "./bin/client_long_requests",
    "./bin/client_mult_sessions",
    "./bin/client_no_release",
    "./bin/client_normal",
    "./bin/client_mult_sessions",
    "./bin/client_normal",
};

void* clients_thread(void* arg) {
    int client_pid[N_CLIENTS];

    for(int i = 0; i < N_CLIENTS; ++i) {
	client_pid[i] = fork();
	if(client_pid[i] != 0) continue;
	if(client_pid[i] < 0) {
	    printf("error forking clients: %i\n", client_pid[i]);
	    break;
	}

	char id_arg[2]; sprintf(id_arg, "%i", i);
	char port_arg[5]; sprintf(port_arg, "%i", 2000+i);
	char* args[] = {clients[i], "./raft_config", id_arg, port_arg, NULL};
	int rs = execv(clients[i], args);
	printf("client exec failed, result: %i\n", rs);
    }

    int status;
    for(int i = 0; i < N_CLIENTS; ++i) {
	waitpid(client_pid[i], &status, 0);
    }

    printf("CLIENTS FINISHED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    int use_backup = 0;
    if(argc > 1 && strcmp(argv[1], "use-backup") == 0) { 
	use_backup = 1;
    }
    printf("use_backup = %s\n", (use_backup) ? "true" : "false");

    start_server_cluster(use_backup); 

    pthread_t tid;
    pthread_create(&tid, NULL, clients_thread, 0);

    start_server_failures(); 
}
