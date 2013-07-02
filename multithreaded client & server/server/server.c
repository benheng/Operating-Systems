/*  A multithreaded server run on an Arch Linux Raspberry Pi using TCP. The port number is
**      predefined but can also be passed as an argument with a little code modification.
**
**  Function: Listens to a port awaiting a connection. When a connection is made, the
**            program creates a new thread to handle each request. The request will consist
**            of strings of integers delimited by " ". Each thread will sum up all the
**            integers sent by the client and store it in a local variable. Once calculated,
**            each thread will update two shared global variables client_count and
**            global_sum. A mutex lock is implemented in order to protect either global data
**            from being written at the same time. Once updated, the global variables are
**            copied to local variables (to ensure thread-safe operations). Finally, the
**            lock is released and all the results are sent to the client.
*
**  BUGS: Since input validation is done in the client application, I intentionally left it
**        out of this program. This can be corrected at a later time.
*/

/* ============ Includes =================================================================== */
#include <errno.h>
#include <fcntl.h>          // file i/o constants
#include <netdb.h>
#include <pthread.h>        // poxis thread implementation
#include <signal.h>         // signal
#include <stdio.h>          // printf()
#include <stdlib.h>         // exit()
#include <string.h>         // memset(), strcpy(), strerror(), strlen()
#include <unistd.h>
#include <arpa/inet.h>      // socket system calls (bind)
#include <netinet/in.h>
#include <sys/socket.h>     // socket system calls
#include <sys/stat.h>       // file i/o constants
#include <sys/types.h>

/* ============ Defines ==================================================================== */
#define MAX_BUFF 2048		// maximum buffer size in bytes
#define STR_PORT_NUM "5795" // (string) port number for server (last 5 digits of my BUID)
#define BACKLOG 10          // how many pending connections to hold

/* ============ Global Variables =========================================================== */
long int client_count = 0, global_sum = 0;
static pthread_mutex_t mutex_locker = PTHREAD_MUTEX_INITIALIZER;

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

/* ======== Child Thread =================================================================== */
void *my_thread(void * socket)
{
    // local variables
    int client_ts;                              // holds a copy of the client socket
    char RxBuff[MAX_BUFF], TxBuff[MAX_BUFF];    // receive/send buffers
    int r_status, s_status;                     // receive/send return values
    long int local_sum = 0;                     // sum of ints provided by single client
    long int local_gbl_sum, local_c_count;      // thread-safe copies
    long int li_value;
    char *s_value = {0};

    // copy the client socket
    client_ts = *(int *)socket;

    // wait to receive data from client
    bzero(RxBuff, MAX_BUFF);
    r_status = recv(client_ts, RxBuff, MAX_BUFF, 0);    // blocking receive
    if (r_status < 0) error("server: recv error");
    printf("server: Receiving transmission\n        [%s]\n", RxBuff);

    // parse buffer and do work
    if (RxBuff != NULL) {
        s_value = strtok(RxBuff, " \t\r\n");
        while (s_value != NULL) {
            li_value = strtol(s_value, NULL, 0);    // bug: validation only done on client
            local_sum += li_value;
            s_value = strtok(NULL, " \t\r\n");
        }
    }

    // lock and edit global variables
    pthread_mutex_lock(&mutex_locker);
        global_sum += local_sum;
        client_count++;
        local_gbl_sum = global_sum;
        local_c_count = client_count;
    pthread_mutex_unlock(&mutex_locker);

    // send data to client
    bzero(TxBuff, MAX_BUFF);
    snprintf(TxBuff, sizeof TxBuff, "server: Your total is: %ld\n"
                "server: The current Grand Total is %ld and I have "
                "served %ld clients so far!\r\n", local_sum, local_gbl_sum, local_c_count);
    s_status = send(client_ts, TxBuff, strlen(TxBuff), 0);
    if (s_status < 0) error ("server: send error");

    // closing statements
    close(client_ts);
    pthread_exit(NULL);
}

/* ======== Main Server Program ============================================================ */
int main(int argc, char *argv[])
{
    // local variables
    int server_s, client_s;                 // server/client socket descriptor
    struct addrinfo hints;                  // structure with relevant info
    struct addrinfo *servinfo, *p;          // points to results
    struct sockaddr_storage cli_addr;       // client's address info
    socklen_t addr_len;                     // address length
    char s[INET6_ADDRSTRLEN];               // holds client IP address
    int gai_status, t_status;               // getaddrinfo/thread return values
    int optval = 1;                         // option value for setsockopt()
    pthread_t tid;                          // thread ID (used by OS)
    int t_arg;                              // thread args

    // set up structures
    memset(&hints, 0, sizeof hints);        // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;            // don't care if IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;        // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;            // automatically fill in IP
    if ((gai_status = getaddrinfo(NULL, STR_PORT_NUM, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_status));
        return 1;
    }

    // loop through all results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            perror("server: socket error");
            continue;
        }
        if (setsockopt(server_s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0)
            error("server: setsockopt error");
        if (bind(server_s, p->ai_addr, p->ai_addrlen) < 0) {
            close(server_s);
            perror("server: bind error");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    freeaddrinfo(servinfo);                 // all done; free this structure

    // listen for connections and accept
    printf("server: Battlecruiser operational\n");
    if (listen(server_s, BACKLOG) < 0) error("server: listen error");

    // main server loop
    while(1) {
        // wait for a client to connect
        printf("server: Hailing frequencies open (waiting for connection)\n");
        addr_len = sizeof cli_addr;
        client_s = accept(server_s, (struct sockaddr *)&cli_addr, &addr_len);
        if (client_s < 0) {
            perror("server: accept error");
            continue;   // don't exit the server program, instead continue to wait
        }

        // print client IP address
        inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr *)&cli_addr), s, sizeof s);
        printf("server: All crews reporting [client %s]\n", s);

        // create a child thread to handle requests
        t_arg = client_s;
        t_status = pthread_create(          // create a child thread
                            &tid,           // thread ID (system assigned)
                            NULL,           // default thread attributes
                            my_thread,      // thread routine
                            &t_arg);        // args to be passed
        if (t_status) error("server: threading error");
    } // main server while loop

    pthread_exit(NULL); // close all threads
    close(server_s);    // close the primary socket
    return 0;           // return code from main
}
