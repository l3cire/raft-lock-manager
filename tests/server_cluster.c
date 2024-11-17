#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#define N_SERVERS 5

int server_pid[N_SERVERS];
int server_active[N_SERVERS];
int nactive;

void start_server_cluster(int use_backup) {
    nactive = N_SERVERS;

    for(int i = 0; i < N_SERVERS; ++i) {
	server_active[i] = 1;
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
}

void start_server_failures() {
    while(1) {
	if(nactive < 5 && ((rand() % 100) < 10)) {
	    int ind = rand() % N_SERVERS;
	    while(server_active[ind] == 1) ind = rand() % N_SERVERS;

	    server_active[ind] = 1;
	    nactive++;

	    server_pid[ind] = fork();
	    if(server_pid[ind] != 0) continue;

	    char id_arg[2]; sprintf(id_arg, "%i", ind+1);
	    char* args[] = {"./raft_config", id_arg, "use-backup", NULL};
	    int rs = execv("./bin/server", args);
	    printf("exec failed, result: %i\n", rs);
	    exit(1);
	}

	if(nactive == 3 || ((rand() % 100) < 95)) {	
	    usleep(100000);
	    continue;
	}

	int ind = rand() % N_SERVERS;
	while(server_active[ind] == 0) ind = rand() % N_SERVERS;

	server_active[ind] = 0;
	nactive --;
	kill(server_pid[ind], SIGKILL);
	printf("killed server %i\n", ind+1);
    }
}