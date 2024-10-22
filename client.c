#include <stdio.h>
#include "rpc.h"

int main(int argc, char *argv[]) {
    rpc_conn_t rpc;
    RPC_init(&rpc, 20000, 10000, "localhost");
    printf("RPC initialization successfull\n");
    
    RPC_close(&rpc);
    printf("Closed RPC\n");
}
