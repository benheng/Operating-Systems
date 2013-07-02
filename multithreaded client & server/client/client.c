/*  A simple client run on a Linux machine using TCP. The port number is predefined but can
**      also be passed as an argument with a little code modification.
**
**  Function: Connects to a server with the given host name and port number. Once connected,
**            instructions are displayed. Error checking and input validation is done for
**            each input before it is concatenated onto the TxBuff. When the input sequence
**            is terminated, the program sends the buffer to the server and awaits a reply.
**
**  Protocol: (string) long integer inputs delimited by " ".
*/

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
#define MAX_TX 2048         // maximum tranfer buffer in bytes
#define MAX_RX 2048         // maximum receive buffer in bytes
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
    char TxBuff[MAX_TX], RxBuff[MAX_RX];// transmit and receive buffers
    int TxOverflow = MAX_TX - 2;        // prevent overflow, 2 bytes reserved for ' \0'
    char *delim = " ";                  // adds ' \0'

    // make sure the user specified a hostname
    if (argc < 2) {
        fprintf(stderr, "usage %s hostname\n", argv[0]);
        exit(1);
    }

    // set up structures
    memset(&hints, 0, sizeof hints);    // make sure the struct is empty
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

    // print server IP address and instructions
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: Good day, commander [server %s]\n", s);
    printf("client: Please enter several integers delimited by [Enter].\n"
           "        Terminate the sequence with a blank line [Enter][Enter].\n"
           "        Supported bases: octal, decimal, hex.\n"
           "        IMPORTANT: octal values prefixed with \"0\"\n"
           "                   decimal digits are not prefixed\n"
           "                   hex digits are prexied with \"0x\" or \"0X\"\n"
           "         OPTIONAL: all bases support \"+\" \"-\"\n");
    freeaddrinfo(servinfo); // release this structure since we are done with it

    // parse input block
    bzero(TxBuff, MAX_TX);
    TxBuff[0] = '\0';
    // NOTE: the only way to exit the while loop is to enter a blank line or
    //       overflow the TxBuff, in which case the last input is ignored
    while(1) {
        // prepare input for validation (removes '\n')
		printf("#: ");
        fflush(NULL);
        char input[MAX_TX] = {0};
        char *token = {0};
        if (!fgets(input, MAX_TX, stdin))
            error("client: input read error");
		token = strtok(input, "\t\r\n");

		// condition testing & input validation
        if (token == NULL) {                        // termination detection
            if (strlen(TxBuff) == 0) strcat(TxBuff, delim);// send a " " at least
            else TxBuff[strlen(TxBuff)-1] = '\0';   // delete the last " "
            printf("client: inputs buffered\n");
            break;                                  // exit while loop
        }
        else if ((TxOverflow-strlen(token))<0){     // TxOverflow detection
            TxBuff[strlen(TxBuff)-1] = '\0';        // delete the last " "
            fprint("client: trasmit buffer overflow detected (ignoring last input)\n");
            break;                                  // exit while loop
        }
        else {                                      // input validation
            // input test block
            errno = 0;
            char *garbage = NULL;
            long l_value = strtol(token, &garbage, 0);  // base can be 8, 10, or 16
            switch (errno) {
                case ERANGE:
                    printf("ERROR: Input cannot be represented as an integer.\n"
                           "       Out of (long int) range. Please try again.\n");
                    break;  // exit switch
                case EINVAL:
                    printf("ERROR: Unsupported base / radix. Please try again.\n");
                    break;  // exit switch
            }
            if (errno != 0) continue;
            if (strlen(garbage) > 0) {
                printf("ERROR: Input contains unsupported characters for base: %s. "
                       "Please try again.\n", garbage);
                continue;
            }

            // only valid inputs make it paste this point
			printf("Read as (base 10): %ld\n", l_value);    // debugging

            // concatenate buffer with input + deliminator
            TxOverflow -= strlen(token) + 1;
            strncat(TxBuff, token, strlen(token));
            strcat(TxBuff, delim);
        }
    } // while(1) parse loop

    // send inputs
    printf("client: transmitting\n        [%s]\n", TxBuff); // debugging
    s_status = send(client_s, TxBuff, strlen(TxBuff), 0);
    if (s_status < 0) error("client: send error");

    // wait to receive response from server
    bzero(RxBuff, MAX_RX);
    r_status = recv(client_s, RxBuff, MAX_RX, 0);
    if (r_status < 0) error ("client: recv error");
    printf("%s", RxBuff);

    close(client_s);
    return 0;
}
