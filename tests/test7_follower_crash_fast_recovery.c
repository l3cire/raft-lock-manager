#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../client_rpc.h"

#include "./server_cluster.c"


raft_configuration_t config;

int client1_state = 0;
int client2_state = 0;


rpc_conn_t rpc1, rpc2;

void* client1(void *arg) {
    RPC_init(&rpc1, 1, 2000, config);
    while(client2_state == 0) {}

    assert(RPC_acquire_lock(&rpc1) == 0);
    printf("client1: lock acquired\n");
    client1_state = 1;

    char buffer[BUFFER_SIZE] = "message from client 1\n";
    assert(RPC_append_file(&rpc1, "file_0", buffer) == 0);
    printf("client1: write success\n");
    client1_state = 2;

    assert(RPC_release_lock(&rpc1) == 0);
    RPC_close(&rpc1);
    pthread_exit(0);
}

void* client2(void *arg) {
    RPC_init(&rpc2, 2, 2001, config);
    client2_state = 1;
    while(client1_state == 0) {}

    assert(RPC_acquire_lock(&rpc2) == 0);
    usleep(500 * 1000);
    printf("client2: lock acquired\n");

    char buffer[BUFFER_SIZE] = "message from client 2\n";
    assert(RPC_append_file(&rpc2, "file_0", buffer) == 0);
    printf("client2: write success\n");

    assert(RPC_release_lock(&rpc2) == 0);
    RPC_close(&rpc2);
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    start_server_cluster(0); 
    
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    pthread_t client1_thread, client2_thread;
    pthread_create(&client1_thread, NULL, client1, NULL);
    pthread_create(&client2_thread, NULL, client2, NULL);
    
    while(client1_state < 2) {}

    restart_server(&rpc1, 0, 0);

    while(1) {}
}