/* Simple Shell Program */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_LINE 128        // maximum length of command
#define MAX_ARGS 32         // maximum number of args
#define MAX_CMDS 16         // maximum number of commands
#define DELIMS " \t\r\n"    // delimiters

// Keep track of attributes of the shell.
pid_t shell_pgid;           // shell process group ID
struct termios shell_tmodes;// shell terminal modes
int shell_terminal;
int shell_is_interactive;

/* ============ Redirects cmd (>)(1>)(2>)(>>)(2>>)(&>)(<) file ============================= */
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
        perror ("fork");
        exit(1);
    } else if (pid == 0) {          // child process

        // Restore interactive and job-control signals.
        signal (SIGINT, SIG_DFL);
        signal (SIGQUIT, SIG_DFL);
        signal (SIGTSTP, SIG_DFL);
        signal (SIGTTIN, SIG_DFL);
        signal (SIGTTOU, SIG_DFL);
        signal (SIGCHLD, SIG_DFL);

        if (std_ioe == 3) {         // used for cmd &> file
            dup2 (fd, 1);           // set stdout to output to file
            dup2 (fd, 2);           // set stderr to output to file
        } else {
            dup2 (fd, std_ioe);     // set std(in/out/err) to output to file
        }
        close(fd);                  // close file decriptor
        execvp (cmd[0], cmd);       // exec command
        perror ("execvp");
        exit(1);
    } else {                        // parent process
        pid_t childPid;
        childPid = wait(&status);
    }
}

/* ============ Multi pipe cmd0 | cmd1 ... | cmdn ================================================= */
void multi_pipe (char *cmds[][MAX_CMDS][MAX_ARGS], int n, int *bg)
{
    pid_t pid;
    int status, i, j;

    // set up all the pipes
    int pipes[n*2];
    for (i = 0; i < n; i++) {
        if (pipe(pipes + (i*2)) < 0) {      // pipe failed
            perror("pipe");
            exit(1);
        }
    }

    // fork the child processes
    for (i = 0; i <= n; i++) {
        if ((pid = fork()) < 0) {           // fork failed
            perror ("fork");
            exit(1);
        } else if (pid == 0) {              // child process

            // Restore interactive and job-control signals.
            signal (SIGINT, SIG_DFL);
            signal (SIGQUIT, SIG_DFL);
            signal (SIGTSTP, SIG_DFL);
            signal (SIGTTIN, SIG_DFL);
            signal (SIGTTOU, SIG_DFL);
            signal (SIGCHLD, SIG_DFL);

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
            perror ("execvp");
            exit(1);
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

/* ============ Run Individual Commands ==================================================== */
void runcmd (char *cmd[])
{
    pid_t pid;
    int status;
    if ((pid = fork()) < 0) {
        perror ("fork");
        exit(1);
    } else if (pid == 0) {          // child fork

        // Restore interactive and job-control signals.
        signal (SIGINT, SIG_DFL);
        signal (SIGQUIT, SIG_DFL);
        signal (SIGTSTP, SIG_DFL);
        signal (SIGTTIN, SIG_DFL);
        signal (SIGTTOU, SIG_DFL);
        signal (SIGCHLD, SIG_DFL);

        execvp(cmd[0], cmd);
        perror("execvp");
        exit(1);                    // make sure to exit
    } else {                        // parent
        pid_t childPid;
        childPid = wait(&status);
    }
}

/* ===========  Main Shell Program ========================================================= */
int main (int argc, char *argv[])
{
    // See if we are running interactively.
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty (shell_terminal);

    if (shell_is_interactive) {
        // Loop until we are in the foreground.
        while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ())) {
            kill (- shell_pgid, SIGTTIN);
        }

        // Ignore interactive and job-control signals.
        signal (SIGINT, SIG_IGN);
        signal (SIGQUIT, SIG_IGN);
        signal (SIGTSTP, SIG_IGN);
        signal (SIGTTIN, SIG_IGN);
        signal (SIGTTOU, SIG_IGN);
        signal (SIGCHLD, SIG_IGN);

        // Put ourselves in our own process group.
        shell_pgid = getpid ();
        if (setpgid (shell_pgid, shell_pgid) < 0) {
            perror ("Couldn't put the shell in its own process group.");
            exit(1);
        }

        // Grab control of the terminal.
        tcsetpgrp (shell_terminal, shell_pgid);

        // Save default terminal attributes for shell.
        tcgetattr (shell_terminal, &shell_tmodes);

        // Special tokens.
        char *special[9] = {"&", ">", "1>", "2>", ">>", "2>>", "&>", "<", "|"};

        // Read the user input and execute jobs.
        while (1) {
            printf ("bsh$ ");
            fflush(stdout);

            // reset user input array
            char input[MAX_LINE] = {0};

            // read the user input
            if (!fgets(input, MAX_LINE, stdin)) {
                printf ("Error reading input.\n");
                break;
            }

            // (Re)initialize variables
            char *stok[(MAX_CMDS)-1] = {0};         // array for redirect tokens
            char *cmd = {0};                        // holds the input string
            char *cmds[MAX_CMDS][MAX_ARGS] = {0};   // array of commands
            int cmdc = 0;                           // reset commands counter
            int tokc = 0;                           // reset token counter
            int bg[MAX_CMDS] = {0};                 // array for & tokens
            int bgflag;                             // bg flag
            int tokflag;                            // redirect token flag
            int run = 0;                            // run flag

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

          // Print useful values for debugging
          // printf("cmds[0]: %s %s %s %s %s\n",cmds[0][0],cmds[0][1],cmds[0][2],cmds[0][3],cmds[0][4]);
          // printf("cmds[1]: %s %s %s %s %s\n",cmds[1][0],cmds[1][1],cmds[1][2],cmds[1][3],cmds[1][4]);
          // printf("     bg: %d %d\n",bg[0], bg[1]);
          // printf("   cmdc: %d\n   tokc: %d\n", cmdc, tokc);
          // printf("   stok: %s %s %s %s\n\n", stok[0], stok[1], stok[2], stok[3]);

                // run commands based on the parsed command line
                if (run) {
                    if (strcmp(cmds[0][0], "exit") == 0) {      // exit command takes priority
                        printf("Exiting shell...\n\n");
                        exit(1);
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
        } // while (1)
    } // if (shell_is_interactive)
} // main (int argc, char *argv[])
