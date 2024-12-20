#include "udp.h"

int packet_loss = 0;
int fail_prob = 0;

// create a socket and bind it to a port on the current machine
// used to listen for incoming packets
int UDP_Open(int port) {
    int fd;           
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	perror("socket");
	return 0;
    }

    // set up the bind
    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));

    my_addr.sin_family      = AF_INET;
    my_addr.sin_port        = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *) &my_addr, sizeof(my_addr)) == -1) {
		perror("bind");
		close(fd);
		return -1;
    }

    return fd;
}

// fill sockaddr_in struct with proper goodies
int UDP_FillSockAddr(struct sockaddr_in *addr, char *hostname, int port) {
    bzero(addr, sizeof(struct sockaddr_in));
    if (hostname == NULL) {
	return 0; // it's OK just to clear the address
    }
    
    addr->sin_family = AF_INET;          // host byte order
    addr->sin_port   = htons(port);      // short, network byte order

    struct in_addr *in_addr;
    struct hostent *host_entry;
    if ((host_entry = gethostbyname(hostname)) == NULL) {
		perror("gethostbyname");
		return -1;
    }
    in_addr = (struct in_addr *) host_entry->h_addr;
    addr->sin_addr = *in_addr;

    return 0;
}

int UDP_SetReceiveTimeout(int fd, int timeout) {
    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
	perror("error setting timeout");
	return -1;
    }
    return 0;
}

int UDP_Write(int fd, struct sockaddr_in *addr, char *buffer, int n) {
    if(packet_loss && ((rand() % 100) < fail_prob)) {
	printf("PACKET LOSS\n");
	return n;
    }
    int addr_len = sizeof(struct sockaddr_in);
    int rc = sendto(fd, buffer, n, 0, (struct sockaddr *) addr, addr_len);
    return rc;
}

int UDP_Read(int fd, struct sockaddr_in *addr, char *buffer, int n) {
    int len = sizeof(struct sockaddr_in); 
    int rc = recvfrom(fd, buffer, n, 0, (struct sockaddr *) addr, (socklen_t *) &len);
    // assert(len == sizeof(struct sockaddr_in)); 
    return rc;
}

int UDP_Close(int fd) {
    return close(fd);
}

void UDP_SimulatePacketLoss(int fp) {
    packet_loss = 1;
    fail_prob = fp;
}
