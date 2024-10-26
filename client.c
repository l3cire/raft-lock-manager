#include <stdio.h>
#include "client_rpc.h"

int main(int argc, char *argv[]) {
    rpc_conn_t rpc;
    RPC_init(&rpc, 20000, 10000, "localhost");
    printf("rpc initialization successfull\n");
    
    RPC_acquire_lock(&rpc);
    RPC_append_file(&rpc, "file_1", "hello world");
    RPC_release_lock(&rpc);

    RPC_close(&rpc);
    printf("closed rpc\n");
}
