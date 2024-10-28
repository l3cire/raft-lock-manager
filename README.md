Below there are the main invariants that need to hold for the client processes:
- Each client issues RPC requests synchronously, e.g. a new remote procedure call can be issued only after the previous one has returned.
    - This implies that a client has to get the response from the server before it can issue the next request
- The first request is always 'RPC_init'
- Each client has a unique client id specified to RPC_init
- Client repeats the same request until the server response that the request has been handled. In particular, client will repeat the request iff (1) the server hasn't responded after a timeout or (2) the server has responded that the request is still being processed. 

Here are the main invariants that must be maintained for the server:
- The server holds information about the clients in the 'client_table' structure. This table is protected by the 'client_table_lock': accessing each element of the table and modifying the table entries should be done only while holding this lock.
- Each client data structure is protected by its own lock. Any access to the client data should be done while holding this lock.
- Each response socket message should be sent while holding a lock of the corresponding client data structure. This ensures the order in which responses are sent.
- If client.state equals to PROCESSING, this means that the client request is being processed on one of the threads of the server.
    - If a new request for a client is received while its state is PROCESSING, this means that the same request was received before and is being processed on another thread.
- client.vtime always equals the virtual time of the client at the moment of last received request. In particular, if client.state is WAITING, then vtime equals the virtual time of the last handled request and client.last_response equals the response of the server for that request.
