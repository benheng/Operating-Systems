// A simple server run on a Raspberry Pi in the internet domain using TCP. The port number is
// predefined but can also be passed as an argument with a little code modification.

/* ============ Includes =================================================================== */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ============ Defines ==================================================================== */
#define MAX_BUFF 1024		// maximum buffer size in bytes
#define STR_PORT_NUM "5795" // (string) port number for server (last 5 digits of my BUID)
#define BACKLOG 10          // how many pending connections to hold
#define MAX_LINE 128        // maximum length of command in bytes
#define MAX_ARGS 32         // maximum number of args
#define MAX_CMDS 16         // maximum number of commands for redirection/piping
#define DELIMS " \t\r\n"    // delimiters

/* ============ Helper Functions =========================================================== */
// print errors and exit
void error(const char *msg)
{
    perror(msg);
    exit(1);
}
// zombie process reaper
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
// get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/* ============	Redirects cmd (>)(1>)(2>)(>>)(2>>)(&>)(<) file ============================= */
void redirect (char *cmd[], char *file, int std_ioe, int append, int bg)
{
    pid_t pid;                      // processor id
    int fd, status;                 // file decriptor and status
    if (std_ioe == 0) {             // stdin selected (cmd < file)
        fd = open(file, O_RDONLY);
    } else if (append) {            // append cmd to file
        fd = open(file, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    } else {                        // truncate
        fd = open(file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    if ((pid = fork()) < 0) {       // fork failed
        error ("fork");
    } else if (pid == 0) {          // child process
        if (std_ioe == 3) {         // used for cmd &> file
            dup2 (fd, 1);           // set stdout to output to file
            dup2 (fd, 2);           // set stderr to output to file
        } else {
            dup2 (fd, std_ioe);     // set std(in/out/err) to output to file
        }
        close(fd);                  // close file decriptor
        execvp (cmd[0], cmd);       // exec command
        error("execvp");
    } else {                        // parent process
        pid_t childPid;
        childPid = wait(&status);
    }
}

/* ============ Multi pipe cmd0 | cmd1 ... | cmdn ========================================== */
void multi_pipe (char *cmds[][MAX_CMDS][MAX_ARGS], int n, int *bg)
{
    pid_t pid;
    int status, i, j;

    // set up all the pipes
    int pipes[n*2];
    for (i = 0; i < n; i++) {
        if (pipe(pipes + (i*2)) < 0) error("pipe"); // pipe failed
    }

    // fork the child processes
    for (i = 0; i <= n; i++) {
        if ((pid = fork()) < 0) {           // fork failed
            error("fork");
        } else if (pid == 0) {              // child process
            if (i != n) {                   // does not apply to last cmd
                dup2 (pipes[(i*2)+1], 1);   // set stdout to pipe output
            }
            if (i != 0) {                   // does not apply to first cmd
                dup2 (pipes[(i*2)-2], 0);   // set stdin to previous pipe input
            }

            // IMPORTANT! close all pipe ends
            for (j = 0; j < n*2; j++) {
                close(pipes[j]);
            }
            execvp (cmds[0][i][0], cmds[0][i]);
            error("execvp");
        }
    }

    // only the parent will reach this part
    // IMPORTANT! close up the pipes
    for (i = 0; i < n*2; i++) {
        close(pipes[i]);
    }
    // wait for all child processes to finish
    for (i = 0; i <= n;  i++) {
        wait(&status);
    }
}

/* ============	Run Individual Commands ==================================================== */
void runcmd (char *cmd[])
{
    pid_t pid;
    int status;
    if ((pid = fork()) < 0) {
        error("fork");
    } else if (pid == 0) {          // child
        execvp(cmd[0], cmd);
        error("execvp");
    } else {                        // parent
        pid_t childPid;
        childPid = wait(&status);
    }
}

/* ============ Parse Buffer =============================================================== */
void parseBuffer (char *input, int sockfd)
{
    // (Re)initialize variables
    char *special[9] = {"&", ">", "1>", "2>", ">>", "2>>", "&>", "<", "|"};
    char *stok[(MAX_CMDS)-1] = {0};         // array for redirect tokens
    char *cmd = {0};                        // holds the input string
    char *cmds[MAX_CMDS][MAX_ARGS] = {0};   // array of commands
    int cmdc = 0;				            // reset commands counter
    int tokc = 0;				            // reset token counter
    int bg[MAX_CMDS] = {0};			        // array for & tokens
    int bgflag;					            // bg flag
    int tokflag;				            // redirect token flag
    int run = 0;                            // run flag
    dup2(sockfd, STDOUT_FILENO);
    dup2(sockfd, STDERR_FILENO);

    // parse the string into seperate tokens and store them into an array
    if (input != NULL && (input[0] != '\n')) {
        run = 1;                            // set the valid run flag
        bg[cmdc] = 0;                       // is it background process?
        cmd = strtok (input, DELIMS);       // retrieve first token
        while (cmd != NULL) {               // parse the input string
            bgflag = 0;                     // bg flag lowered
            tokflag = 0;                    // redirect token flag lowered
            int ii;                         // special token detection starts here
            for (ii = 0; ii < 9; ii++) {    // IMPORTANT: range should equal length of array
                if (strcmp(cmd, special[ii]) == 0) {    // is it a special token?
                    if (ii == 0) {          // is it an ampersand?
                        bg[cmdc] = 1;       // this command will execute in the bg
                        bgflag++;           // raise bg flag
                        break;
                    } else {                // if not, then it must be a redirect token
                        stok[cmdc] = malloc (strlen(special[ii])+1);    // allocate space for token
                        strcpy (stok[cmdc], special[ii]);   // store token in stok
                        tokflag++;          // raise the redirect token flag
                        break;
                    }
                }
            }
            if (tokflag) {                  // special token detected
                cmds[cmdc][tokc] = malloc (1);  // allocate memory for string terminator
                cmds[cmdc][tokc] = '\0';    // store string terminator
                cmdc++;                     // increase command count
                bg[cmdc] = 0;               // set next process as foreground process
                tokc = 0;                   // reset the token counter
            } else if (bgflag == 0) {       // if bg flag raised, skip this
                cmds[cmdc][tokc] = malloc (strlen(cmd)+1);  // allocate mem for token
                strcpy (cmds[cmdc][tokc], cmd); // store command
                tokc++;                     // increase token count
            }
            cmd = strtok (NULL, DELIMS);    // retrieve the next token
        } // parser block

        // run commands based on the parsed command line
        if (run) {
            if (strcmp(cmds[0][0], "exit") == 0) {      // exit command takes priority
                return;
            } else if (cmdc) {  // Redirections         // prevents segfault, if # cmds > 1
                if (strcmp(stok[0], ">") == 0) {        // redirect stdout of cmd to file
                    redirect(cmds[0], cmds[1][0], 1, 0, bg[0]);
                } else if (strcmp(stok[0], "1>") == 0) {// same as above, 1 is default fd
                    redirect(cmds[0], cmds[1][0], 1, 0, bg[0]);
                } else if (strcmp(stok[0], "2>") == 0) {// redirect stderr of cmd to a file
                    redirect(cmds[0], cmds[1][0], 2, 0, bg[0]);
                } else if (strcmp(stok[0], ">>") == 0) {// append stdout of cmd to a file
                    redirect(cmds[0], cmds[1][0], 1, 1, bg[0]);
                } else if (strcmp(stok[0], "2>>") == 0){// append stderr of cmd to a file
                    redirect(cmds[0], cmds[1][0], 2, 1, bg[0]);
                } else if (strcmp(stok[0], "&>") == 0) {// redirect stdout and stderr of cmd to a file
                    redirect(cmds[0], cmds[1][0], 3, 0, bg[0]);
                } else if (strcmp(stok[0], "<") == 0) { // redirect contents of file to stdin of cmd
                    redirect(cmds[0], cmds[1][0], 0, 0, bg[0]);
                } else if (strcmp(stok[0], "|") == 0) { // set up (multiple) pipes
                    multi_pipe(&cmds, cmdc, bg);
                } else {                                // if unable to detect special token
                    runcmd(cmds[0]);                    // try to run single command
                }
            } else {
                runcmd(cmds[0]);                        // run single command
            }
        } // run commands block */
    } // if (input != NULL)
}

/* ======== Main Server Program ============================================================ */
int main(int argc, char *argv[])
{
    // local variables
    int server_s, client_s;                 // server and client socket descriptor
    struct addrinfo hints;                  // structure with relevant info
    struct addrinfo *servinfo, *p;          // points to results
    struct sockaddr_storage cli_addr;       // client's address info
    socklen_t addr_len;                     // address length
    char s[INET6_ADDRSTRLEN];               // address string
    char RxBuffer[MAX_LINE];                // receive buffer
    char DtBuffer[MAX_BUFF];                // buffer containing the date
    struct sigaction sa;                    // examine and change a signal action
    int gai_status, s_status, r_status;     // getaddrinfo/send/receive return values
    int optval = 1;                         // option value for setsockopt()
    time_t ticks;                           // used for time calculation

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

    // reap all dead processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) error("server: sigaction error");

    // main server loop
    while(1) {
        // wait for a client to connect
        printf("server: Hailing frequencies open\n");
        addr_len = sizeof cli_addr;
        client_s = accept(server_s, (struct sockaddr *)&cli_addr, &addr_len);
        if (client_s < 0) {
            perror("server: accept error");
            continue;
        }

        inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr *)&cli_addr), s, sizeof s);
        printf("server: All crews reporting [client %s]\n", s);

        // create a new process to handle requests
        pid_t pid;
        if ((pid = fork()) < 0) error("server: fork error");// fork failed
        else if (pid == 0) {                                // child process
            // close the listening socket for the child
            close(server_s);

            // receive command from client
            bzero(RxBuffer, MAX_LINE);                      // clear receive buffer
            r_status = recv(client_s,RxBuffer,MAX_LINE,0);  // read
            if (r_status < 0) error("server: recv error");
            printf("server: Receiving transmission [client %s]\n", s);
            printf("        client: %s", RxBuffer);

            // parse and execute client command
            parseBuffer(RxBuffer, client_s);

            // respond to client
            s_status = send(client_s, "server: Engage! ", 16, 0);
            if (s_status < 0) error ("server: send response error");

            // send time to client
            bzero(DtBuffer, MAX_BUFF);     // clear date buffer
            ticks = time(NULL);
            snprintf(DtBuffer, sizeof DtBuffer, "[%.24s]\r\n", ctime(&ticks));
            s_status = send(client_s, DtBuffer, strlen(DtBuffer), 0);
            if (s_status < 0) error ("server: send time error");

            // send final message
            s_status = send(client_s, "server: request completed.\n", 27, 0);
            if (s_status < 0) error ("server: send completed error");

            // close the client socket and exit
            close(client_s);
            exit(0);
        }
        // close the client socket for the parent
        close(client_s);
    } // main server while loop

    close(server_s);    // close the primary socket
    return 0;           // return code from main
}
