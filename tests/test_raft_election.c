#include "../raft.h"
#include "../udp.h"
#include <stdio.h>

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
    
    int id = 1;
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
    raft_state_t raft;
    bzero(&raft, sizeof(raft_state_t));
    if(1) {
	Raft_server_init(&raft, config, id, 20000+id-1);
        Raft_RPC_listen(&raft);
    } else {
	while(1) {}
    }
}
