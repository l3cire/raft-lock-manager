#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../client_rpc.h"

#include "./server_cluster.c"


raft_configuration_t config;

int client_state = 0;

void* client(void *arg) {
    rpc_conn_t rpc;
    RPC_init(&rpc, 1, 2000, config);

    assert(RPC_acquire_lock(&rpc) == 0);
    printf("client1: lock acquired\n");

    char buffer[BUFFER_SIZE] = "message from client 1\n";
    assert(RPC_append_file(&rpc, "file_0", buffer) == 0);
    printf("client1: first write\n");
    client_state = 1;
    usleep(10 * 1000);
    assert(RPC_append_file(&rpc, "file_0", buffer) == E_LOCK_EXP);
    printf("client1: second write failed -- server crashed, lock lost\n");

    assert(RPC_acquire_lock(&rpc) == 0);
    printf("client1: lock acquired\n");
    assert(RPC_append_file(&rpc, "file_0", buffer) == 0);
    printf("client1: third write\n");
    assert(RPC_release_lock(&rpc) == 0);
    RPC_close(&rpc);
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    start_server_cluster(0); 
    
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    pthread_t client_thread;
    pthread_create(&client_thread, NULL, client, NULL);

    while(client_state == 0) {}
    kill_all_servers(); 
    printf("SERVERS KILLED\n");
    start_server_cluster(1);

    while(1) {}
}
