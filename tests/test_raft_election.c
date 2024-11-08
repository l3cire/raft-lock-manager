#include "../raft.h"
#include "../udp.h"
#include "pthread.h"
#include <stdio.h>

int id;
raft_state_t raft;

void* raft_listener_thread(void* arg) {
    Raft_RPC_listen(&raft);
    pthread_exit(0);
}

void commit_handler(char data[LOG_BUFFER_SIZE]) {
    printf("(%i) COMMIT HANDLER: %s\n", id, data);
}

int main(int argc, char* argv[]) {
    raft_configuration_t config;
    for(int i = 0; i < N_SERVERS; ++i) {
	config.ids[i] = i+1;
	UDP_FillSockAddr(&config.servers[i], "localhost", 20000+i);
    }

    int pid1 = fork();
    int pid2 = 0;
    int pid3 = 0;
    int pid4 = 0;
    
    id = 1;
    if(pid1 == 0) {
	id = 2;
    } else {
	pid2 = fork();
	if(pid2 == 0) {
	    id = 3;
	}  else {
	    pid3 = fork();
	    if(pid3 == 0) {
		id = 4;
	    } else {
		pid4 = fork();
		if(pid4 == 0) {
		    id = 5;
		}
	    }
	} 
    }


    sleep(1);
    bzero(&raft, sizeof(raft_state_t));
    if(id == 1 || id == 3) {
	//while(1) {}
	sleep(5);
    }
    Raft_server_init(&raft, config, commit_handler, id, 20000+id-1);
    pthread_t tid;
    pthread_create(&tid, NULL, raft_listener_thread, NULL);
    while(raft.state != LEADER) {}
    sleep(2);
    printf("ADDING NEW ENTRY\n");
    char buf[LOG_BUFFER_SIZE];
    sprintf(buf, "hello world");
    Raft_append_entry(&raft, id, 0, buf);
    while(1) {}
}
