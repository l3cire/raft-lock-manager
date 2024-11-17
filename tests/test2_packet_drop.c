#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../client_rpc.h"

#include "./server_cluster.c"


raft_configuration_t config;

void* client1(void *arg) {
    rpc_conn_t rpc;
    RPC_init(&rpc, 1, 2000, config);
    assert(RPC_acquire_lock(&rpc) == 0);

    char buffer[BUFFER_SIZE] = "message from client 1\n";
    assert(RPC_append_file(&rpc, "file_0", buffer) == 0);
    assert(RPC_release_lock(&rpc) == 0);
    RPC_close(&rpc);

    printf("client1 finished successfully\n");

    pthread_exit(0);
}

void* client2(void *arg) {
    rpc_conn_t rpc;
    RPC_init(&rpc, 2, 2001, config);
    assert(RPC_acquire_lock(&rpc) == 0);

    char buffer[BUFFER_SIZE] = "message from client 2\n";
    assert(RPC_append_file(&rpc, "file_0", buffer) == 0);
    assert(RPC_release_lock(&rpc) == 0);
    RPC_close(&rpc);

    printf("client2 finished successfully\n");

    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    start_server_cluster(0); 

    UDP_SimulatePacketLoss(30);
    
    FILE *f = fopen("./raft_config", "rb");
    fread(&config, sizeof(raft_configuration_t), 1, f);
    fclose(f);

    pthread_t client1_thread, client2_thread;
    pthread_create(&client1_thread, NULL, client1, NULL);
    pthread_create(&client2_thread, NULL, client2, NULL);

    while(1) {}
}
