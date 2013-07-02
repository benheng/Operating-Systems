// A simple client run on a Linux machine using TCP. The port number is predefined but
// can also be passed as an argument with a little code modification.

/* ============ Includes =================================================================== */
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

/* ============ Defines ==================================================================== */
#define MAX_TX 1024         // maximum transfer buffer in bytes
#define MAX_RX 16384        // maximum receive buffer in bytes
#define STR_PORT_NUM "5795" // (string) port number for client (last 5 digits of my BUID)

/* ============ Helper Functions =========================================================== */
// print errors and exit
void error(const char *msg)
{
    perror(msg);
    exit(1);
}
// get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// ============ Main Client Program ======================================================== */
int main(int argc, char *argv[])
{
    // local variables
    int client_s;                       // client socket descriptor
    struct addrinfo hints;              // structure with relevant info
    struct addrinfo *servinfo, *p;      // points to results
    int gai_status, s_status, r_status; // getaddrinfo/send/receive return values
    char s[INET6_ADDRSTRLEN];           // address string
    char TxBuffer[MAX_TX], RxBuffer[MAX_RX];// transmit and receive buffers

    // make sure the user specified a hostname
    if (argc < 2) {
        fprintf(stderr, "usage %s hostname\n", argv[0]);
        exit(1);
    }

    // set up structures
    memset(&hints, 0, sizeof hints);     // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;        // don't care if IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP stream sockets
    if ((gai_status = getaddrinfo(argv[1], STR_PORT_NUM, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_status));
        return 1;
    }

    // loop through all results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((client_s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            perror("client: socket error");
            continue;
        }
        if (connect(client_s, p->ai_addr, p->ai_addrlen) < 0) {
            close(client_s);
            perror("client: connect error");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    // print server IP address
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: Good day, commander [server %s]\n", s);
    printf("client: Set a course $ ");
    freeaddrinfo(servinfo); // release this structure since we are done with it

    // send commands to server
    bzero(TxBuffer, MAX_TX);
    fgets(TxBuffer, MAX_TX, stdin);
    s_status = send(client_s, TxBuffer, strlen(TxBuffer), 0);
    if (s_status < 0) error("client: send error");

    // receive response from server
    do {
        bzero(RxBuffer, MAX_RX);
        r_status = recv(client_s, RxBuffer, MAX_RX, 0);
        if (r_status < 0) error ("client: recv error");
        printf("%s", RxBuffer);
    } while (!strstr(RxBuffer, "request completed."));

    close(client_s);
    return 0;
}
